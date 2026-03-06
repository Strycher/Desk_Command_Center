"""In-memory TTL cache for adapter data."""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import Any


@dataclass
class CacheEntry:
    value: dict[str, Any]
    expires_at: float
    stored_at: float = field(default_factory=time.monotonic)

    @property
    def is_expired(self) -> bool:
        return time.monotonic() > self.expires_at


class TTLCache:
    """Thread-safe (GIL-protected) TTL cache keyed by adapter/source name."""

    def __init__(self) -> None:
        self._entries: dict[str, CacheEntry] = {}

    def get(self, key: str) -> dict[str, Any] | None:
        entry = self._entries.get(key)
        if entry is None or entry.is_expired:
            return None
        return entry.value

    def set(self, key: str, value: dict[str, Any], ttl: int) -> None:
        now = time.monotonic()
        self._entries[key] = CacheEntry(
            value=value,
            expires_at=now + ttl,
            stored_at=now,
        )

    def get_age(self, key: str) -> float | None:
        """Seconds since this key was last written, or None if missing."""
        entry = self._entries.get(key)
        if entry is None:
            return None
        return time.monotonic() - entry.stored_at

    def get_entry(self, key: str) -> CacheEntry | None:
        return self._entries.get(key)

    def keys(self) -> list[str]:
        return list(self._entries.keys())

    def delete(self, key: str) -> None:
        self._entries.pop(key, None)

    def clear(self) -> None:
        self._entries.clear()
