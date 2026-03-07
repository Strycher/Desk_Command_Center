"""Unfocused Tasks adapter — polls the Unfocused REST API for pending tasks."""

from __future__ import annotations

import logging
from typing import Any

import httpx

from .base import AdapterConfig, BaseAdapter

logger = logging.getLogger(__name__)

# Map Unfocused priority strings to firmware uint8_t values.
# Firmware: 0=none, 1=high, 2=medium, 3=low
_PRIORITY_MAP = {
    "P0": 1,  # Critical → high
    "P1": 1,  # High
    "P2": 2,  # Medium
    "P3": 3,  # Low
}


def _parse_priority(priority_str: str | None) -> int:
    """Extract priority level from strings like 'P2 - Medium'."""
    if not priority_str:
        return 0
    prefix = priority_str[:2]  # "P0", "P1", etc.
    return _PRIORITY_MAP.get(prefix, 0)


class UnfocusedTasksAdapter(BaseAdapter):
    """Polls the Unfocused REST API for task data.

    Config from BridgeConfig:
      unfocused_tasks.api_url:  base URL (default https://getunfocused.app)
      unfocused_tasks.api_key:  Bearer token (uf_... format)
      unfocused_tasks.statuses: status filter list
      unfocused_tasks.limit:    max tasks per request
      unfocused_tasks.poll_interval: seconds between polls
    """

    def __init__(self, bridge_config: dict[str, Any]) -> None:
        uf_cfg = bridge_config.get("unfocused_tasks", {})
        poll_interval = uf_cfg.get("poll_interval", 300)

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
        self.api_url = uf_cfg.get("api_url", "https://getunfocused.app")
        self.api_key = uf_cfg.get("api_key", "")
        self.statuses = uf_cfg.get("statuses", ["not_started", "in_progress"])
        self.limit = uf_cfg.get("limit", 50)

    async def poll(self) -> dict[str, Any]:
        if not self.api_key:
            raise ValueError("unfocused_tasks.api_key not configured")

        url = f"{self.api_url.rstrip('/')}/api/tasks"
        params = {
            "status": ",".join(self.statuses),
            "limit": self.limit,
        }
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
        meta = raw.get("meta", {})

        tasks = []
        for t in raw_tasks:
            tasks.append({
                "title": t.get("title", ""),
                "due_date": t.get("due_date", ""),
                "priority": _parse_priority(t.get("priority")),
                "source": "unfocused",
                "completed": t.get("status") == "completed",
            })

        return {
            "tasks": tasks,
            "total": meta.get("total", len(tasks)),
        }
