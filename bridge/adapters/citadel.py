"""Citadel adapter — polls task management API for project health."""

from __future__ import annotations

import logging
from typing import Any

import httpx

from .base import AdapterConfig, BaseAdapter

logger = logging.getLogger(__name__)

DEFAULT_API_URL = "https://getunfocused.app/citadel"


class CitadelAdapter(BaseAdapter):
    """Polls Citadel REST API for task status across all projects.

    Config from BridgeConfig:
      citadel.api_url: Citadel API base URL
      citadel.projects: list of project names (auto-discovered if empty)
      citadel.poll_interval: seconds between polls

    Comprehensive polling: fetches stats (all statuses) + active tasks
    per project. Output shape matches legacy BeadsAdapter for display
    compatibility.
    """

    def __init__(self, source_config: dict[str, Any]) -> None:
        poll_interval = source_config.get("poll_interval", 60)

        super().__init__(
            name="citadel",
            config=AdapterConfig(
                poll_interval=poll_interval,
                ttl=poll_interval * 3,
                max_retries=2,
                backoff_base=5.0,
                backoff_max=120.0,
            ),
        )
        self.api_url = source_config.get("api_url", DEFAULT_API_URL).rstrip("/")
        self.projects: list[str] = source_config.get("projects", [])

    async def poll(self) -> dict[str, Any]:
        async with httpx.AsyncClient(timeout=15.0) as client:
            # Health check
            health = await client.get(f"{self.api_url}/health")
            health.raise_for_status()

            # Discover projects if not configured
            project_names = self.projects
            if not project_names:
                resp = await client.get(f"{self.api_url}/projects")
                resp.raise_for_status()
                project_names = [p["name"] for p in resp.json()]

            results: dict[str, Any] = {}
            for name in project_names:
                try:
                    # Stats: full status breakdown in one call
                    stats_resp = await client.get(
                        f"{self.api_url}/projects/{name}/stats",
                    )
                    stats_resp.raise_for_status()
                    stats = stats_resp.json()

                    # Active tasks: details for in_progress items
                    tasks_resp = await client.get(
                        f"{self.api_url}/projects/{name}/tasks",
                        params={"status": "in_progress"},
                    )
                    tasks_resp.raise_for_status()
                    tasks = tasks_resp.json()

                    results[name] = {
                        "stats": stats,
                        "tasks": tasks,
                    }
                except httpx.HTTPStatusError as exc:
                    logger.warning("Citadel: %s returned %d", name, exc.response.status_code)
                    results[name] = {"error": str(exc)}
                except httpx.RequestError as exc:
                    logger.warning("Citadel: %s request failed: %s", name, exc)
                    results[name] = {"error": str(exc)}

        return {"projects": results}

    def parse(self, raw: dict[str, Any]) -> dict[str, Any]:
        """Transform raw Citadel API responses into BeadsAdapter-compatible shape.

        Output:
          {
            "projects": {
              "Unfocused": {
                "status": "ok",
                "display_name": "Unfocused",
                "open": N,           # backlog + todo + ready
                "in_progress": N,
                "blocked": 0,
                "done": N,
                "total": N,
                "by_status": {...},
                "tasks": [...]
              }
            },
            "total_open": N,
            "total_in_progress": N,
            "blocked_count": 0
          }
        """
        projects: dict[str, Any] = {}
        total_open = 0
        total_in_progress = 0
        total_blocked = 0

        for name, data in raw.get("projects", {}).items():
            if "error" in data:
                projects[name] = {
                    "status": "error",
                    "error": data["error"],
                    "tasks": [],
                }
                continue

            stats = data.get("stats", {})
            by_status = stats.get("by_status", {})
            tasks_raw = data.get("tasks", [])

            backlog = by_status.get("backlog", 0)
            todo = by_status.get("todo", 0)
            ready = by_status.get("ready", 0)
            in_progress = by_status.get("in_progress", 0)
            testing = by_status.get("testing", 0)
            done = by_status.get("done", 0)
            deferred = by_status.get("deferred", 0)
            blocked = by_status.get("blocked", 0)

            # "open" = actionable work not yet done
            open_count = backlog + todo + ready + testing

            tasks = [
                {
                    "id": t.get("short_id", ""),
                    "title": t.get("title", ""),
                    "status": t.get("status", ""),
                    "priority": t.get("priority"),
                    "assignee": t.get("assignee"),
                    "external_ref": t.get("external_issue_number"),
                    "type": t.get("issue_type", "task"),
                }
                for t in tasks_raw
            ]

            total_open += open_count
            total_in_progress += in_progress
            total_blocked += blocked

            projects[name] = {
                "status": "ok",
                "display_name": name,
                "open": open_count,
                "in_progress": in_progress,
                "blocked": blocked,
                "done": done,
                "deferred": deferred,
                "total": stats.get("total", 0),
                "by_status": by_status,
                "tasks": tasks,
            }

        return {
            "projects": projects,
            "total_open": total_open,
            "total_in_progress": total_in_progress,
            "blocked_count": total_blocked,
        }
