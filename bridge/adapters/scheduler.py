"""Async scheduler that runs adapters on their configured intervals."""

from __future__ import annotations

import asyncio
import logging
import time
from typing import Any

from .base import AdapterStatus, BaseAdapter
from .cache import TTLCache

logger = logging.getLogger(__name__)


class AdapterScheduler:
    """Manages adapter lifecycle: registration, polling, caching, error isolation.

    Each adapter runs in its own asyncio task so a failure in one
    never impacts another.
    """

    def __init__(self, cache: TTLCache | None = None) -> None:
        self.cache = cache or TTLCache()
        self._adapters: dict[str, BaseAdapter] = {}
        self._tasks: dict[str, asyncio.Task[None]] = {}
        self._running = False

    def register(self, adapter: BaseAdapter) -> None:
        if adapter.name in self._adapters:
            raise ValueError(f"Adapter {adapter.name!r} already registered")
        self._adapters[adapter.name] = adapter
        logger.info("Registered adapter: %s", adapter.name)

    def get_adapter(self, name: str) -> BaseAdapter | None:
        return self._adapters.get(name)

    def list_adapters(self) -> list[dict[str, Any]]:
        result = []
        for adapter in self._adapters.values():
            age = self.cache.get_age(adapter.name)
            result.append({
                "name": adapter.name,
                "status": adapter.state.status.value,
                "last_ok": adapter.state.last_ok,
                "last_error": adapter.state.last_error,
                "error_message": adapter.state.error_message,
                "consecutive_failures": adapter.state.consecutive_failures,
                "total_polls": adapter.state.total_polls,
                "total_errors": adapter.state.total_errors,
                "cache_age_seconds": round(age, 1) if age is not None else None,
                "poll_interval": adapter.config.poll_interval,
            })
        return result

    async def start(self) -> None:
        if self._running:
            return
        self._running = True
        for name, adapter in self._adapters.items():
            task = asyncio.create_task(
                self._run_adapter(adapter),
                name=f"adapter-{name}",
            )
            self._tasks[name] = task
        logger.info("Scheduler started with %d adapters", len(self._adapters))

    async def stop(self) -> None:
        self._running = False
        for task in self._tasks.values():
            task.cancel()
        if self._tasks:
            await asyncio.gather(*self._tasks.values(), return_exceptions=True)
        self._tasks.clear()
        logger.info("Scheduler stopped")

    async def poll_once(self, adapter: BaseAdapter) -> dict[str, Any] | None:
        """Execute a single poll+parse cycle with retries. Returns parsed data or None."""
        adapter.state.total_polls += 1

        for attempt in range(adapter.config.max_retries + 1):
            try:
                raw = await adapter.poll()
                parsed = adapter.parse(raw)

                # Success
                adapter.state.status = AdapterStatus.OK
                adapter.state.last_ok = time.time()
                adapter.state.error_message = None
                adapter.state.consecutive_failures = 0

                self.cache.set(adapter.name, parsed, adapter.config.ttl)
                logger.debug("Adapter %s polled OK", adapter.name)
                return parsed

            except Exception as exc:
                adapter.state.total_errors += 1
                adapter.state.last_error = time.time()
                adapter.state.error_message = str(exc)

                if attempt < adapter.config.max_retries:
                    delay = adapter.backoff_delay(attempt)
                    logger.warning(
                        "Adapter %s attempt %d failed (%s), retrying in %.1fs",
                        adapter.name, attempt + 1, exc, delay,
                    )
                    await asyncio.sleep(delay)
                else:
                    adapter.state.consecutive_failures += 1
                    adapter.state.status = AdapterStatus.ERROR
                    logger.error(
                        "Adapter %s failed after %d attempts: %s",
                        adapter.name, adapter.config.max_retries + 1, exc,
                    )
        return None

    async def _run_adapter(self, adapter: BaseAdapter) -> None:
        """Long-running loop for a single adapter."""
        while self._running:
            await self.poll_once(adapter)

            # Check staleness: if cache has expired, mark stale
            entry = self.cache.get_entry(adapter.name)
            if entry is not None and entry.is_expired:
                if adapter.state.status != AdapterStatus.ERROR:
                    adapter.state.status = AdapterStatus.STALE

            try:
                await asyncio.sleep(adapter.config.poll_interval)
            except asyncio.CancelledError:
                break
