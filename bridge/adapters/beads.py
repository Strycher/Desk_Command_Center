"""Beads/Dolt adapter — queries task status from Dolt via MySQL protocol."""

from __future__ import annotations

import logging
from typing import Any

from .base import AdapterConfig, BaseAdapter

logger = logging.getLogger(__name__)


class BeadsAdapter(BaseAdapter):
    """Polls Beads issues from Dolt database via SSH tunnel.

    Config from BridgeConfig:
      beads.host: tunnel host (default 127.0.0.1)
      beads.port: tunnel port (default 3310)
      beads.projects: list of database names (auto-discovered if empty)
      beads.poll_interval: seconds between polls
    """

    def __init__(self, bridge_config: dict[str, Any]) -> None:
        beads_cfg = bridge_config.get("beads", {})
        poll_interval = beads_cfg.get("poll_interval", 120)

        super().__init__(
            name="beads",
            config=AdapterConfig(
                poll_interval=poll_interval,
                ttl=poll_interval * 3,
                max_retries=1,
                backoff_base=10.0,
                backoff_max=60.0,
            ),
        )
        self.host = beads_cfg.get("host", "127.0.0.1")
        self.port = beads_cfg.get("port", 3310)
        self.projects: list[str] = beads_cfg.get("projects", [])

    async def poll(self) -> dict[str, Any]:
        import pymysql

        databases = self.projects
        if not databases:
            databases = self._discover_databases()

        results: dict[str, list[dict]] = {}
        for db_name in databases:
            try:
                conn = pymysql.connect(
                    host=self.host,
                    port=self.port,
                    user="root",
                    database=db_name,
                    connect_timeout=5,
                    read_timeout=10,
                )
                try:
                    cur = conn.cursor()
                    cur.execute(
                        "SELECT id, title, status, priority, assignee, external_ref, issue_type "
                        "FROM issues "
                        "WHERE status IN ('open', 'in_progress') "
                        "ORDER BY priority ASC, created_at ASC "
                        "LIMIT 50",
                    )
                    rows = cur.fetchall()
                    results[db_name] = [
                        {
                            "id": row[0],
                            "title": row[1],
                            "status": row[2],
                            "priority": row[3],
                            "assignee": row[4],
                            "external_ref": row[5],
                            "type": row[6],
                        }
                        for row in rows
                    ]
                    cur.close()
                finally:
                    conn.close()
            except Exception as exc:
                logger.warning("Beads: failed to query %s: %s", db_name, exc)
                results[db_name] = [{"error": str(exc)}]

        return {"projects": results}

    def _discover_databases(self) -> list[str]:
        """Find all beads_* databases on the Dolt server."""
        import pymysql

        conn = pymysql.connect(
            host=self.host,
            port=self.port,
            user="root",
            connect_timeout=5,
        )
        try:
            cur = conn.cursor()
            cur.execute("SHOW DATABASES")
            dbs = [row[0] for row in cur.fetchall() if row[0].startswith("beads_")]
            cur.close()
            return dbs
        finally:
            conn.close()

    def parse(self, raw: dict[str, Any]) -> dict[str, Any]:
        projects = {}
        total_open = 0
        total_in_progress = 0

        for project_name, tasks in raw.get("projects", {}).items():
            # Check for error entries
            if tasks and isinstance(tasks[0], dict) and "error" in tasks[0]:
                projects[project_name] = {
                    "status": "error",
                    "error": tasks[0]["error"],
                    "tasks": [],
                }
                continue

            open_tasks = [t for t in tasks if t["status"] == "open"]
            in_progress = [t for t in tasks if t["status"] == "in_progress"]
            total_open += len(open_tasks)
            total_in_progress += len(in_progress)

            # Clean up project name for display (strip beads_ prefix)
            display_name = project_name
            if display_name.startswith("beads_"):
                display_name = display_name[6:]

            projects[project_name] = {
                "status": "ok",
                "display_name": display_name,
                "open": len(open_tasks),
                "in_progress": len(in_progress),
                "tasks": tasks,
            }

        return {
            "projects": projects,
            "total_open": total_open,
            "total_in_progress": total_in_progress,
        }
