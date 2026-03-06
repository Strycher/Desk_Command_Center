"""Runtime configuration for DCC Bridge.

Stores settings in memory with a simple JSON file for persistence.
Secrets (API keys) are masked in GET responses.
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

DEFAULT_CONFIG: dict[str, Any] = {
    "bridge": {
        "push_ttl": 600,
    },
    "display": {
        "poll_interval": 30,
        "timezone": "America/New_York",
    },
    "weather": {
        "provider": "openweathermap",
        "api_key": "",
        "lat": 39.3451,
        "lon": -84.3916,
        "units": "imperial",
        "poll_interval": 900,
    },
    "home_assistant": {
        "url": "",
        "api_key": "",
        "poll_interval": 60,
    },
    "google_calendar": {
        "client_id": "",
        "client_secret": "",
        "refresh_token": "",
        "calendars": ["primary"],
        "poll_interval": 300,
    },
    "github": {
        "api_key": "",
        "repos": ["Strycher/Desk_Command_Center", "Strycher/Field_Compass"],
        "org_api_key": "",
        "org_repos": ["DifferentWire/Unfocused"],
        "poll_interval": 300,
    },
    "beads": {
        "host": "dcc-dolt",
        "port": 3306,
        "projects": [],
        "poll_interval": 120,
    },
}

CONFIG_PATH = Path(__file__).parent / "bridge_config.json"


class BridgeConfig:
    """In-memory config with JSON file persistence and secret masking."""

    def __init__(self, path: Path | None = None) -> None:
        self._path = path or CONFIG_PATH
        self._data: dict[str, Any] = {}
        self._load()

    def _load(self) -> None:
        """Load from disk, falling back to defaults."""
        if self._path.exists():
            try:
                self._data = json.loads(self._path.read_text())
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

    def get_all(self, mask_secrets: bool = True) -> dict[str, Any]:
        """Return full config, optionally masking secret values."""
        if not mask_secrets:
            return self._data
        return self._mask(self._data)

    def get_section(self, section: str) -> dict[str, Any] | None:
        """Return a config section by name, or None if missing."""
        return self._data.get(section)

    def update(self, updates: dict[str, Any]) -> dict[str, list[str]]:
        """Apply partial updates. Returns {updated: [keys], errors: [messages]}.

        Accepts top-level section keys with dicts of values to merge.
        Unknown top-level keys are rejected.
        """
        updated = []
        errors = []

        for section, values in updates.items():
            if section not in self._data:
                errors.append(f"Unknown section: {section!r}")
                continue
            if not isinstance(values, dict):
                errors.append(f"Section {section!r} must be an object")
                continue

            for key, value in values.items():
                if key not in self._data[section]:
                    errors.append(f"Unknown key: {section}.{key}")
                    continue

                expected_type = type(self._data[section][key])
                if expected_type is not type(None) and not isinstance(value, expected_type):
                    # Allow int where float is expected and vice versa
                    if isinstance(value, (int, float)) and expected_type in (int, float):
                        value = expected_type(value)
                    else:
                        errors.append(
                            f"Type mismatch for {section}.{key}: "
                            f"expected {expected_type.__name__}, got {type(value).__name__}",
                        )
                        continue

                self._data[section][key] = value
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
