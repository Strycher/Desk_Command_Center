"""Home Assistant adapter — fetches entity states via REST API."""

from __future__ import annotations

import logging
from typing import Any

import httpx

from .base import AdapterConfig, BaseAdapter

logger = logging.getLogger(__name__)

# Entity domains we care about for the display
DISPLAY_DOMAINS = {
    "climate", "light", "switch", "lock", "cover",
    "sensor", "binary_sensor", "person", "media_player",
}


class HomeAssistantAdapter(BaseAdapter):
    """Polls Home Assistant REST API for entity states.

    Config from BridgeConfig:
      home_assistant.url: HA base URL (e.g. http://192.168.50.24:8123)
      home_assistant.api_key: long-lived access token
      home_assistant.poll_interval: seconds between polls
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

    async def poll(self) -> dict[str, Any]:
        if not self.url or not self.token:
            raise ValueError("Home Assistant URL and API key are required")

        headers = {
            "Authorization": f"Bearer {self.token}",
            "Content-Type": "application/json",
        }
        async with httpx.AsyncClient(timeout=10.0, headers=headers) as client:
            resp = await client.get(f"{self.url}/api/states")
            resp.raise_for_status()
            return {"states": resp.json()}

    def parse(self, raw: dict[str, Any]) -> dict[str, Any]:
        states = raw.get("states", [])

        # Group entities by domain, filtering to display-relevant ones
        by_domain: dict[str, list[dict]] = {}
        for entity in states:
            entity_id = entity.get("entity_id", "")
            domain = entity_id.split(".")[0]
            if domain not in DISPLAY_DOMAINS:
                continue

            attrs = entity.get("attributes", {})
            parsed_entity = {
                "entity_id": entity_id,
                "state": entity.get("state"),
                "friendly_name": attrs.get("friendly_name", entity_id),
                "last_changed": entity.get("last_changed"),
            }

            # Domain-specific attributes
            if domain == "climate":
                parsed_entity.update({
                    "current_temp": attrs.get("current_temperature"),
                    "target_temp": attrs.get("temperature"),
                    "hvac_action": attrs.get("hvac_action"),
                    "preset_mode": attrs.get("preset_mode"),
                })
            elif domain == "sensor":
                parsed_entity.update({
                    "unit": attrs.get("unit_of_measurement"),
                    "device_class": attrs.get("device_class"),
                })
            elif domain == "person":
                parsed_entity.update({
                    "source": attrs.get("source"),
                })
            elif domain == "media_player":
                parsed_entity.update({
                    "media_title": attrs.get("media_title"),
                    "app_name": attrs.get("app_name"),
                })

            by_domain.setdefault(domain, []).append(parsed_entity)

        return {
            "domains": by_domain,
            "total_entities": sum(len(v) for v in by_domain.values()),
        }
