"""Home Assistant adapter — fetches entity states via REST API,
filters by DCC label via WebSocket entity registry, and groups
entities by device for consolidated dashboard cards.

When a "DCC" label exists in HA and entities are labeled with it,
only those entities are included (label mode). Otherwise, falls back
to the legacy domain-based filter (domain mode).
"""

from __future__ import annotations

import json
import logging
import time
from typing import Any

import httpx

from .base import AdapterConfig, BaseAdapter

logger = logging.getLogger(__name__)

# Legacy fallback: entity domains shown when no DCC label is configured
DISPLAY_DOMAINS = {
    "climate", "light", "switch", "lock", "cover",
    "sensor", "binary_sensor", "person", "media_player",
    "device_tracker",
}


class HomeAssistantAdapter(BaseAdapter):
    """Polls Home Assistant REST API for entity states.

    Config from BridgeConfig:
      home_assistant.url: HA base URL (e.g. http://192.168.50.24:8123)
      home_assistant.api_key: long-lived access token
      home_assistant.poll_interval: seconds between polls
      home_assistant.label: label name to filter by (default "DCC")
      home_assistant.registry_ttl: seconds to cache registry data (default 300)
    """

    def __init__(self, bridge_config: dict[str, Any]) -> None:
        ha_cfg = bridge_config.get("home_assistant", {})
        poll_interval = ha_cfg.get("poll_interval", 60)

        super().__init__(
            name="home_assistant",
            config=AdapterConfig(
                poll_interval=poll_interval,
                ttl=poll_interval * 3,
                max_retries=2,
                backoff_base=5.0,
                backoff_max=60.0,
            ),
        )
        self.url = ha_cfg.get("url", "").rstrip("/")
        self.token = ha_cfg.get("api_key", "")
        self.label_name = ha_cfg.get("label", "DCC")
        self._registry_ttl = ha_cfg.get("registry_ttl", 300)

        # Cached registry data (refreshed every _registry_ttl seconds)
        self._registry: dict[str, Any] | None = None
        self._registry_ts: float = 0

    # ── WebSocket registry helpers ────────────────────────────────

    async def _ws_command(self, ws: Any, msg_id: int, msg_type: str) -> dict:
        """Send a WebSocket command and wait for its response by ID."""
        await ws.send(json.dumps({"id": msg_id, "type": msg_type}))
        while True:
            raw = await ws.recv()
            resp = json.loads(raw)
            if resp.get("id") == msg_id:
                return resp
            # Skip unsolicited messages (events, pings, etc.)

    async def _fetch_registries(self) -> dict[str, Any] | None:
        """Fetch label, entity, and device registries via HA WebSocket API.

        Returns a dict with:
          dcc_entity_ids: set of entity_id strings with the DCC label
          entity_info: {entity_id: {"device_id": str|None}} for DCC entities
          device_names: {device_id: str} mapping
        Or None if the DCC label doesn't exist or WebSocket fails.
        """
        try:
            import websockets  # noqa: delay import — only needed for registry
        except ImportError:
            logger.error("websockets package not installed — registry fetch disabled")
            return None

        ws_url = self.url.replace("http://", "ws://").replace("https://", "wss://")
        ws_url += "/api/websocket"

        try:
            async with websockets.connect(ws_url) as ws:
                # Step 1: Wait for auth_required
                msg = json.loads(await ws.recv())
                if msg.get("type") != "auth_required":
                    logger.warning("HA WS: unexpected initial message: %s", msg.get("type"))
                    return None

                # Step 2: Authenticate
                await ws.send(json.dumps({
                    "type": "auth",
                    "access_token": self.token,
                }))
                msg = json.loads(await ws.recv())
                if msg.get("type") != "auth_ok":
                    logger.error("HA WS: auth failed — %s", msg.get("message", "unknown"))
                    return None

                # Step 3: Fetch all three registries
                labels_resp = await self._ws_command(ws, 1, "config/label_registry/list")
                entities_resp = await self._ws_command(ws, 2, "config/entity_registry/list")
                devices_resp = await self._ws_command(ws, 3, "config/device_registry/list")

            # Find the DCC label ID
            dcc_label_id = None
            for label in labels_resp.get("result", []):
                if label.get("name", "").lower() == self.label_name.lower():
                    dcc_label_id = label["label_id"]
                    break

            if not dcc_label_id:
                logger.info("HA: no '%s' label found — falling back to domain mode",
                            self.label_name)
                return None

            # Build entity→info mapping for DCC-labeled entities
            dcc_entity_ids: set[str] = set()
            entity_info: dict[str, dict] = {}
            for entry in entities_resp.get("result", []):
                entry_labels = entry.get("labels", [])
                if dcc_label_id in entry_labels:
                    eid = entry.get("entity_id", "")
                    dcc_entity_ids.add(eid)
                    entity_info[eid] = {
                        "device_id": entry.get("device_id"),
                    }

            # Build device_id→name mapping
            device_names: dict[str, str] = {}
            for device in devices_resp.get("result", []):
                did = device.get("id")
                name = (device.get("name_by_user")
                        or device.get("name")
                        or "Unknown Device")
                device_names[did] = name

            logger.info("HA: registry fetched — %d DCC entities, %d devices",
                        len(dcc_entity_ids), len(device_names))
            return {
                "dcc_entity_ids": dcc_entity_ids,
                "entity_info": entity_info,
                "device_names": device_names,
            }

        except Exception as exc:
            logger.warning("HA: WebSocket registry fetch failed: %s", exc)
            return None

    # ── Poll ──────────────────────────────────────────────────────

    async def poll(self) -> dict[str, Any]:
        if not self.url or not self.token:
            raise ValueError("Home Assistant URL and API key are required")

        # Refresh registry cache if stale
        now = time.time()
        if self._registry is None or (now - self._registry_ts) > self._registry_ttl:
            reg = await self._fetch_registries()
            if reg is not None:
                self._registry = reg
                self._registry_ts = now
            elif self._registry is not None:
                # Keep stale cache rather than losing label mode
                logger.info("HA: registry refresh failed — using stale cache")

        # Fetch current states via REST
        headers = {
            "Authorization": f"Bearer {self.token}",
            "Content-Type": "application/json",
        }
        async with httpx.AsyncClient(timeout=10.0, headers=headers) as client:
            resp = await client.get(f"{self.url}/api/states")
            resp.raise_for_status()
            return {"states": resp.json(), "registry": self._registry}

    # ── Parse ─────────────────────────────────────────────────────

    def parse(self, raw: dict[str, Any]) -> dict[str, Any]:
        states = raw.get("states", [])
        registry = raw.get("registry")

        if registry and registry.get("dcc_entity_ids"):
            return self._parse_label_mode(states, registry)
        return self._parse_domain_mode(states)

    def _parse_entity(self, entity: dict, domain: str) -> dict:
        """Extract normalized entity dict with domain-specific attributes."""
        attrs = entity.get("attributes", {})
        parsed: dict[str, Any] = {
            "entity_id": entity.get("entity_id", ""),
            "state": entity.get("state"),
            "friendly_name": attrs.get("friendly_name", entity.get("entity_id", "")),
            "domain": domain,
        }

        if domain == "climate":
            parsed.update({
                "current_temp": attrs.get("current_temperature"),
                "target_temp": attrs.get("temperature"),
                "hvac_action": attrs.get("hvac_action"),
                "preset_mode": attrs.get("preset_mode"),
            })
        elif domain in ("sensor", "binary_sensor"):
            parsed.update({
                "unit": attrs.get("unit_of_measurement"),
                "device_class": attrs.get("device_class"),
            })
        elif domain == "person":
            parsed.update({
                "source": attrs.get("source"),
            })
        elif domain == "media_player":
            parsed.update({
                "media_title": attrs.get("media_title"),
                "app_name": attrs.get("app_name"),
            })
        elif domain == "device_tracker":
            parsed.update({
                "source_type": attrs.get("source_type"),
            })

        return parsed

    def _parse_label_mode(self, states: list, registry: dict) -> dict[str, Any]:
        """Parse with DCC label filtering and device grouping."""
        dcc_ids = registry["dcc_entity_ids"]
        entity_info = registry["entity_info"]
        device_names = registry["device_names"]

        # Filter to DCC-labeled entities and parse them
        device_groups: dict[str, list[dict]] = {}  # device_id → entities
        standalone: list[dict] = []

        for entity in states:
            eid = entity.get("entity_id", "")
            if eid not in dcc_ids:
                continue

            domain = eid.split(".")[0]
            parsed = self._parse_entity(entity, domain)

            # Group by device_id
            info = entity_info.get(eid, {})
            device_id = info.get("device_id")

            if device_id:
                device_groups.setdefault(device_id, []).append(parsed)
            else:
                standalone.append(parsed)

        # Build devices list with names
        devices = []
        for did, entities in device_groups.items():
            devices.append({
                "device_name": device_names.get(did, "Unknown Device"),
                "device_id": did,
                "entities": entities,
            })

        # Sort devices by name for stable ordering
        devices.sort(key=lambda d: d["device_name"].lower())

        # Build backward-compatible domains dict (flat domain grouping)
        # so older firmware that only knows about domains{} still works
        by_domain: dict[str, list[dict]] = {}
        for d in devices:
            for ent in d["entities"]:
                by_domain.setdefault(ent["domain"], []).append(ent)
        for ent in standalone:
            by_domain.setdefault(ent["domain"], []).append(ent)

        total = sum(len(d["entities"]) for d in devices) + len(standalone)
        logger.info("HA: label mode — %d devices, %d standalone, %d total",
                     len(devices), len(standalone), total)

        return {
            "label_mode": True,
            "label": self.label_name,
            "devices": devices,
            "standalone": standalone,
            "domains": by_domain,
            "total_entities": total,
            "total_devices": len(devices),
        }

    def _parse_domain_mode(self, states: list) -> dict[str, Any]:
        """Legacy parse: filter by DISPLAY_DOMAINS, group by domain."""
        by_domain: dict[str, list[dict]] = {}
        for entity in states:
            entity_id = entity.get("entity_id", "")
            domain = entity_id.split(".")[0]
            if domain not in DISPLAY_DOMAINS:
                continue
            parsed = self._parse_entity(entity, domain)
            by_domain.setdefault(domain, []).append(parsed)

        return {
            "label_mode": False,
            "domains": by_domain,
            "total_entities": sum(len(v) for v in by_domain.values()),
        }
