"""Abstract base adapter for data sources."""

from __future__ import annotations

import logging
from abc import ABC, abstractmethod
from dataclasses import dataclass
from enum import Enum
from typing import Any

logger = logging.getLogger(__name__)


class AdapterStatus(str, Enum):
    OK = "ok"
    ERROR = "error"
    STALE = "stale"
    IDLE = "idle"


@dataclass
class AdapterState:
    status: AdapterStatus = AdapterStatus.IDLE
    last_ok: float | None = None
    last_error: float | None = None
    error_message: str | None = None
    consecutive_failures: int = 0
    total_polls: int = 0
    total_errors: int = 0


@dataclass
class AdapterConfig:
    poll_interval: int = 300       # seconds between polls
    ttl: int = 600                 # cache TTL in seconds
    max_retries: int = 3           # retries per poll cycle
    backoff_base: float = 2.0      # exponential backoff base
    backoff_max: float = 300.0     # max backoff delay in seconds


class BaseAdapter(ABC):
    """Base class for all data source adapters.

    Subclasses must implement poll() and parse().
    The scheduler calls poll() on the configured interval,
    passes the result to parse(), and stores parsed data in the cache.
    """

    def __init__(self, name: str, config: AdapterConfig | None = None) -> None:
        self.name = name
        self._cache_key: str | None = None
        self.config = config or AdapterConfig()
        self.state = AdapterState()

    @property
    def cache_key(self) -> str:
        """Key used by the scheduler for cache reads/writes and registration.

        Defaults to self.name for backward compatibility.
        Per-user adapters set this to "source:user_id".
        """
        return self._cache_key if self._cache_key is not None else self.name

    @cache_key.setter
    def cache_key(self, value: str) -> None:
        self._cache_key = value

    @abstractmethod
    async def poll(self) -> dict[str, Any]:
        """Fetch raw data from the external source.

        Returns raw data dict. Raises on failure.
        """

    @abstractmethod
    def parse(self, raw: dict[str, Any]) -> dict[str, Any]:
        """Transform raw poll response into normalized format.

        Must be a pure function — no I/O, no side effects.
        Returns the data dict to be cached.
        """

    def backoff_delay(self, attempt: int) -> float:
        """Exponential backoff with cap."""
        delay = self.config.backoff_base ** attempt
        return min(delay, self.config.backoff_max)

    def __repr__(self) -> str:
        return f"<{self.__class__.__name__} name={self.name!r} status={self.state.status.value}>"
