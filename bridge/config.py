"""Runtime configuration for DCC Bridge.

Stores settings in memory with a simple JSON file for persistence.
Secrets (API keys) are masked in GET responses.

Supports two config schemas:
- **Legacy (flat):** Top-level source sections (weather, github, etc.)
- **Multi-user (nested):** users/devices/shared_sources structure

Legacy configs are auto-migrated to the nested schema on load.
"""

from __future__ import annotations

import json
import logging
import re
from pathlib import Path
from typing import Any

logger = logging.getLogger(__name__)

# Keys that contain sensitive values — masked in GET responses
SECRET_KEYS = re.compile(
    r"(api_key|secret|token|password|credential)", re.IGNORECASE,
)

# Source keys that are per-user in multi-user mode
_PER_USER_SOURCES = {
    "weather", "google_calendar", "github", "beads",
    "unfocused_tasks", "claude", "claude_status", "devops",
}

# Source keys that are shared across all users
_SHARED_SOURCES = {"home_assistant"}

# Legacy flat config — used as fallback when no file exists
DEFAULT_CONFIG: dict[str, Any] = {
    "bridge": {
        "push_ttl": 600,
    },
    "display": {
        "poll_interval": 30,
        "timezone": "America/New_York",
    },
    "shared_sources": {
        "home_assistant": {
            "url": "",
            "api_key": "",
            "poll_interval": 60,
        },
    },
    "users": {},
    "devices": {},
    "unregistered_devices": {},
}

CONFIG_PATH = Path(__file__).parent / "bridge_config.json"


def _migrate_flat_to_nested(flat: dict[str, Any]) -> dict[str, Any]:
    """Convert legacy flat config to nested multi-user schema.

    Moves per-user sources under users.strycher.sources and shared
    sources under shared_sources. Non-source sections (bridge, display)
    are preserved at the top level.
    """
    nested: dict[str, Any] = {}

    # Preserve non-source top-level sections
    for key in ("bridge", "display"):
        if key in flat:
            nested[key] = flat[key]

    # Build shared_sources
    nested["shared_sources"] = {}
    for key in _SHARED_SOURCES:
        if key in flat:
            nested["shared_sources"][key] = flat[key]

    # Build users.strycher.sources from remaining source sections
    user_sources: dict[str, Any] = {}
    for key in _PER_USER_SOURCES:
        if key in flat:
            user_sources[key] = flat[key]

    if user_sources:
        nested["users"] = {
            "strycher": {
                "display_name": "Chris",
                "sources": user_sources,
            },
        }
    else:
        nested["users"] = {}

    nested["devices"] = {}
    nested["unregistered_devices"] = {}

    logger.info("Migrated legacy flat config to nested multi-user schema")
    return nested


def _is_nested_schema(data: dict[str, Any]) -> bool:
    """Detect whether config uses the nested multi-user schema."""
    return "users" in data or "shared_sources" in data


class BridgeConfig:
    """In-memory config with JSON file persistence and secret masking.

    Supports both legacy flat and nested multi-user config schemas.
    Legacy configs are auto-migrated on load.
    """

    def __init__(self, path: Path | None = None) -> None:
        self._path = path or CONFIG_PATH
        self._data: dict[str, Any] = {}
        self._load()

    @property
    def is_multi_user(self) -> bool:
        """True if config uses the nested multi-user schema."""
        return _is_nested_schema(self._data)

    def _load(self) -> None:
        """Load from disk, migrating legacy format if needed."""
        if self._path.exists():
            try:
                raw = json.loads(self._path.read_text())
                if _is_nested_schema(raw):
                    self._data = raw
                else:
                    self._data = _migrate_flat_to_nested(raw)
                logger.info("Config loaded from %s", self._path)
                return
            except (json.JSONDecodeError, OSError) as exc:
                logger.warning("Failed to load config (%s), using defaults", exc)
        self._data = json.loads(json.dumps(DEFAULT_CONFIG))  # deep copy

    def _save(self) -> None:
        """Persist current config to disk."""
        try:
            self._path.write_text(json.dumps(self._data, indent=2))
        except OSError as exc:
            logger.error("Failed to save config: %s", exc)

    # --- Read accessors ---

    def get_all(self, mask_secrets: bool = True) -> dict[str, Any]:
        """Return full config, optionally masking secret values."""
        if not mask_secrets:
            return self._data
        return self._mask(self._data)

    def get_section(self, section: str) -> dict[str, Any] | None:
        """Return a top-level config section by name, or None if missing."""
        return self._data.get(section)

    def get_user(self, user_id: str) -> dict[str, Any] | None:
        """Return a user's full config block, or None if not found."""
        return self._data.get("users", {}).get(user_id)

    def get_user_sources(self, user_id: str) -> dict[str, Any]:
        """Return a user's sources dict, or empty dict if not found."""
        user = self.get_user(user_id)
        if user is None:
            return {}
        return user.get("sources", {})

    def get_user_source(self, user_id: str, source: str) -> dict[str, Any] | None:
        """Return a specific source config for a user."""
        return self.get_user_sources(user_id).get(source)

    def get_shared_sources(self) -> dict[str, Any]:
        """Return the shared_sources dict."""
        return self._data.get("shared_sources", {})

    def get_device(self, chip_id: str) -> dict[str, Any] | None:
        """Look up a device by chip ID. Returns device record or None."""
        return self._data.get("devices", {}).get(chip_id)

    def resolve_device_user(self, chip_id: str) -> str | None:
        """Given a chip ID, return the user_id it's assigned to, or None."""
        device = self.get_device(chip_id)
        if device is None:
            return None
        return device.get("user")

    def list_users(self) -> list[str]:
        """Return list of user IDs."""
        return list(self._data.get("users", {}).keys())

    def record_unregistered_device(
        self, chip_id: str, source_ip: str,
    ) -> None:
        """Record an unknown device in unregistered_devices."""
        from datetime import datetime, timezone
        unreg = self._data.setdefault("unregistered_devices", {})
        unreg[chip_id] = {
            "first_seen": datetime.now(timezone.utc).isoformat(),
            "source_ip": source_ip,
        }
        self._save()
        logger.warning("Unknown device %s from %s", chip_id, source_ip)

    def get_cache_keys_for_user(self, user_id: str) -> dict[str, str]:
        """Build source->cache_key mapping for a user.

        Per-user sources get cache_key = "source:user_id".
        Shared sources get cache_key = bare source name.
        """
        keys: dict[str, str] = {}
        for source in self.get_user_sources(user_id):
            keys[source] = f"{source}:{user_id}"
        for source in self.get_shared_sources():
            keys[source] = source
        return keys

    def get_cache_keys_shared_only(self) -> dict[str, str]:
        """Cache keys for unregistered devices (shared sources only)."""
        return {source: source for source in self.get_shared_sources()}

    # --- Write operations ---

    def update(self, updates: dict[str, Any]) -> dict[str, list[str]]:
        """Apply partial updates to top-level sections.

        Returns {updated: [keys], errors: [messages]}.
        Only allows updates to bridge, display, and shared_sources.
        User-specific config changes go through update_config.sh on Pi 5.
        """
        updated = []
        errors = []

        # Restrict REST-updateable sections
        allowed_sections = {"bridge", "display", "shared_sources"}

        for section, values in updates.items():
            if section not in allowed_sections:
                errors.append(
                    f"Section {section!r} cannot be updated via REST API"
                )
                continue
            if section not in self._data:
                errors.append(f"Unknown section: {section!r}")
                continue
            if not isinstance(values, dict):
                errors.append(f"Section {section!r} must be an object")
                continue

            target = self._data[section]
            for key, value in values.items():
                if key not in target:
                    errors.append(f"Unknown key: {section}.{key}")
                    continue

                expected_type = type(target[key])
                if expected_type is not type(None) and not isinstance(value, expected_type):
                    if isinstance(value, (int, float)) and expected_type in (int, float):
                        value = expected_type(value)
                    else:
                        errors.append(
                            f"Type mismatch for {section}.{key}: "
                            f"expected {expected_type.__name__}, "
                            f"got {type(value).__name__}",
                        )
                        continue

                target[key] = value
                updated.append(f"{section}.{key}")

        if updated:
            self._save()
            logger.info("Config updated: %s", ", ".join(updated))

        return {"updated": updated, "errors": errors}

    @staticmethod
    def _mask(obj: Any, _parent_key: str = "") -> Any:
        """Recursively mask secret values."""
        if isinstance(obj, dict):
            return {
                k: ("***" if SECRET_KEYS.search(k) and v else BridgeConfig._mask(v, k))
                for k, v in obj.items()
            }
        return obj
