"""Unfocused Tasks adapter — polls the Unfocused REST API for pending tasks."""

from __future__ import annotations

import logging
from typing import Any

import httpx

from .base import AdapterConfig, BaseAdapter

logger = logging.getLogger(__name__)

# Statuses considered "active" (shown in task list)
_ACTIVE_STATUSES = {"not_started", "in_progress"}
# Statuses considered "deferred" (counted but not shown)
_DEFERRED_STATUSES = {"on_hold"}


class UnfocusedTasksAdapter(BaseAdapter):
    """Polls the Unfocused REST API for task data.

    Config from BridgeConfig:
      unfocused_tasks.api_url:  base URL (default https://getunfocused.app)
      unfocused_tasks.api_key:  Bearer token (uf_... format)
      unfocused_tasks.statuses: status filter list
      unfocused_tasks.limit:    max tasks per request
      unfocused_tasks.poll_interval: seconds between polls
    """

    def __init__(self, source_config: dict[str, Any]) -> None:
        poll_interval = source_config.get("poll_interval", 300)

        super().__init__(
            name="unfocused_tasks",
            config=AdapterConfig(
                poll_interval=poll_interval,
                ttl=poll_interval * 2,
                max_retries=2,
                backoff_base=5.0,
                backoff_max=120.0,
            ),
        )
        self.api_url = source_config.get("api_url", "https://getunfocused.app")
        self.api_key = source_config.get("api_key", "")
        self.limit = source_config.get("limit", 50)

    async def poll(self) -> dict[str, Any]:
        if not self.api_key:
            raise ValueError("unfocused_tasks.api_key not configured")

        url = f"{self.api_url.rstrip('/')}/api/tasks"
        params = {"limit": self.limit}
        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Accept": "application/json",
        }

        async with httpx.AsyncClient(timeout=15.0) as client:
            resp = await client.get(url, params=params, headers=headers)
            resp.raise_for_status()
            return resp.json()

    def parse(self, raw: dict[str, Any]) -> dict[str, Any]:
        raw_tasks = raw.get("tasks", [])

        tasks = []
        deferred_count = 0
        for t in raw_tasks:
            status = t.get("status", "")
            if status in _DEFERRED_STATUSES:
                deferred_count += 1
                continue
            if status not in _ACTIVE_STATUSES:
                continue
            tasks.append({
                "title": t.get("title", ""),
                "due_date": t.get("due_date", ""),
                "priority": t.get("priority", ""),
                "source": "unfocused",
                "completed": status == "completed",
            })

        return {
            "tasks": tasks,
            "total": len(tasks),
            "deferred_count": deferred_count,
        }
