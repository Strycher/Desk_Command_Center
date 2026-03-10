"""Persistent file-backed store for push-ingested data.

Writes each push source to a JSON file so data survives bridge restarts.
"""

import json
import logging
from pathlib import Path
from typing import Any

logger = logging.getLogger(__name__)

# Default directory inside the container; mounted as a Docker volume.
DEFAULT_DIR = Path("/app/data")


class PushStore:
    """Read/write push data to individual JSON files."""

    def __init__(self, directory: str | Path = DEFAULT_DIR) -> None:
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)

    def _path(self, key: str) -> Path:
        # Sanitize key to safe filename
        safe = key.replace("/", "_").replace("\\", "_")
        return self.directory / f"{safe}.json"

    def save(self, key: str, data: dict[str, Any]) -> None:
        """Persist push data to disk."""
        path = self._path(key)
        try:
            path.write_text(json.dumps(data), encoding="utf-8")
            logger.debug("PushStore: saved %s (%d bytes)", key, path.stat().st_size)
        except Exception as exc:
            logger.error("PushStore: failed to save %s: %s", key, exc)

    def load(self, key: str) -> dict[str, Any] | None:
        """Load push data from disk, or None if missing."""
        path = self._path(key)
        if not path.exists():
            return None
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            logger.info("PushStore: restored %s from disk", key)
            return data
        except Exception as exc:
            logger.error("PushStore: failed to load %s: %s", key, exc)
            return None

    def load_all(self) -> dict[str, dict[str, Any]]:
        """Load all persisted push sources."""
        results = {}
        for path in self.directory.glob("*.json"):
            key = path.stem
            data = self.load(key)
            if data is not None:
                results[key] = data
        return results
