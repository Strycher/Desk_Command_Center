"""Claude status adapter — queries Agent Mail for active agent sessions."""

from __future__ import annotations

import asyncio
import logging
import time
from typing import Any

import httpx

from .base import AdapterConfig, BaseAdapter

logger = logging.getLogger(__name__)

# Agents inactive for longer than this are "idle"; double this = "offline"
IDLE_THRESHOLD_SECONDS = 600  # 10 minutes
OFFLINE_THRESHOLD_SECONDS = 1800  # 30 minutes


class ClaudeStatusAdapter(BaseAdapter):
    """Polls Agent Mail HTTP API for registered Claude agents.

    Falls back to local process check if Agent Mail is unreachable.

    Config from BridgeConfig:
      claude_status.agent_mail_url: Agent Mail HTTP base URL (optional)
      claude_status.project_key: project identifier for Agent Mail queries
      claude_status.poll_interval: seconds between polls (default 120)
    """

    def __init__(self, source_config: dict[str, Any]) -> None:
        poll_interval = source_config.get("poll_interval", 120)

        super().__init__(
            name="claude_status",
            config=AdapterConfig(
                poll_interval=poll_interval,
                ttl=poll_interval * 3,
                max_retries=1,
                backoff_base=10.0,
                backoff_max=60.0,
            ),
        )
        self.agent_mail_url: str = source_config.get("agent_mail_url", "")
        self.project_key: str = source_config.get(
            "project_key", "/c/Dev/Desk_Command_Center",
        )

    async def poll(self) -> dict[str, Any]:
        """Try Agent Mail HTTP, fall back to local process check."""
        agents: list[dict[str, Any]] = []
        source = "none"

        # Strategy 1: Agent Mail HTTP API
        if self.agent_mail_url:
            try:
                agents = await self._poll_agent_mail()
                source = "agent_mail"
            except Exception as exc:
                logger.warning("Agent Mail unreachable (%s), trying process check", exc)

        # Strategy 2: Local process check (fallback)
        if not agents:
            try:
                agents = await self._poll_local_processes()
                if agents:
                    source = "process_check"
            except Exception as exc:
                logger.warning("Process check failed: %s", exc)

        return {"agents": agents, "source": source}

    async def _poll_agent_mail(self) -> list[dict[str, Any]]:
        """Query Agent Mail HTTP API for registered agents."""
        url = f"{self.agent_mail_url.rstrip('/')}/api/agents"
        async with httpx.AsyncClient(timeout=10.0) as client:
            resp = await client.get(url, params={"project": self.project_key})
            resp.raise_for_status()
            data = resp.json()

        agents = []
        for agent in data.get("agents", []):
            last_active = agent.get("last_active_ts", "")
            agents.append({
                "name": agent.get("name", "unknown"),
                "task": agent.get("task_description", ""),
                "program": agent.get("program", ""),
                "model": agent.get("model", ""),
                "last_active": last_active,
                "source": "agent_mail",
            })
        return agents

    async def _poll_local_processes(self) -> list[dict[str, Any]]:
        """Check for running claude-code processes on the local machine."""
        try:
            proc = await asyncio.create_subprocess_exec(
                "pgrep", "-af", "claude",
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
            stdout, _ = await asyncio.wait_for(proc.communicate(), timeout=5.0)
        except (FileNotFoundError, asyncio.TimeoutError):
            return []

        agents = []
        if stdout:
            lines = stdout.decode().strip().split("\n")
            for line in lines:
                # Each line: "PID command args..."
                parts = line.strip().split(None, 1)
                if len(parts) < 2:
                    continue
                cmd = parts[1]
                # Filter to actual claude-code processes
                if "claude" not in cmd.lower():
                    continue
                agents.append({
                    "name": f"local-{parts[0]}",
                    "task": "",
                    "program": "claude-code",
                    "model": "",
                    "last_active": "",
                    "source": "process_check",
                })
        return agents

    def parse(self, raw: dict[str, Any]) -> dict[str, Any]:
        """Normalize agent data into display-friendly format."""
        agents = raw.get("agents", [])
        source = raw.get("source", "none")

        sessions = []
        active_count = 0
        idle_count = 0
        offline_count = 0

        for agent in agents:
            status = self._classify_status(agent)
            if status == "active":
                active_count += 1
            elif status == "idle":
                idle_count += 1
            else:
                offline_count += 1

            sessions.append({
                "agent": agent.get("name", "unknown"),
                "task": agent.get("task", ""),
                "program": agent.get("program", ""),
                "model": agent.get("model", ""),
                "status": status,
                "source": agent.get("source", source),
            })

        # Overall status: active if any agent is active, idle if any idle, else offline
        if active_count > 0:
            overall_status = "active"
        elif idle_count > 0:
            overall_status = "idle"
        elif sessions:
            overall_status = "offline"
        else:
            overall_status = "offline"

        current_task = ""
        if sessions:
            # Pick the most recently active agent's task
            active_sessions = [s for s in sessions if s["status"] == "active"]
            if active_sessions:
                current_task = active_sessions[0]["task"]

        return {
            "status": overall_status,
            "current_task": current_task,
            "active_count": active_count,
            "idle_count": idle_count,
            "total_count": len(sessions),
            "data_source": source,
            "sessions": sessions,
        }

    @staticmethod
    def _classify_status(agent: dict[str, Any]) -> str:
        """Classify an agent as active, idle, or offline.

        For Agent Mail agents: based on last_active timestamp.
        For process-check agents: always "active" (process is running).
        """
        if agent.get("source") == "process_check":
            return "active"

        last_active = agent.get("last_active", "")
        if not last_active:
            return "active"  # no timestamp = assume active (just registered)

        try:
            from datetime import datetime, timezone
            if isinstance(last_active, str):
                # Parse ISO-8601 timestamp
                ts = datetime.fromisoformat(last_active.replace("Z", "+00:00"))
                age_seconds = (datetime.now(timezone.utc) - ts).total_seconds()
            else:
                age_seconds = time.time() - float(last_active)
        except (ValueError, TypeError):
            return "active"  # can't parse = assume active

        if age_seconds < IDLE_THRESHOLD_SECONDS:
            return "active"
        elif age_seconds < OFFLINE_THRESHOLD_SECONDS:
            return "idle"
        else:
            return "offline"
