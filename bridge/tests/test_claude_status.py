"""Tests for Claude status adapter."""

from __future__ import annotations

from datetime import datetime, timedelta, timezone
from unittest.mock import AsyncMock, patch

import pytest

from adapters.claude_status import ClaudeStatusAdapter


@pytest.fixture
def adapter():
    """Create a ClaudeStatusAdapter with default config."""
    return ClaudeStatusAdapter({
        "agent_mail_url": "http://localhost:9000",
        "project_key": "/test/project",
        "poll_interval": 60,
    })


@pytest.fixture
def adapter_no_url():
    """Create a ClaudeStatusAdapter without Agent Mail URL (fallback only)."""
    return ClaudeStatusAdapter({
        "project_key": "/test/project",
        "poll_interval": 60,
    })


class TestClaudeStatusAdapterInit:
    def test_name(self, adapter):
        assert adapter.name == "claude_status"

    def test_config_from_source(self, adapter):
        assert adapter.config.poll_interval == 60
        assert adapter.agent_mail_url == "http://localhost:9000"
        assert adapter.project_key == "/test/project"

    def test_defaults(self):
        a = ClaudeStatusAdapter({})
        assert a.config.poll_interval == 120
        assert a.agent_mail_url == ""
        assert a.project_key == "/c/Dev/Desk_Command_Center"


class TestParse:
    def test_empty_agents(self, adapter):
        result = adapter.parse({"agents": [], "source": "none"})
        assert result["status"] == "offline"
        assert result["active_count"] == 0
        assert result["total_count"] == 0
        assert result["sessions"] == []

    def test_active_agent_from_process_check(self, adapter):
        raw = {
            "agents": [{
                "name": "local-1234",
                "task": "",
                "program": "claude-code",
                "model": "",
                "last_active": "",
                "source": "process_check",
            }],
            "source": "process_check",
        }
        result = adapter.parse(raw)
        assert result["status"] == "active"
        assert result["active_count"] == 1
        assert result["sessions"][0]["status"] == "active"

    def test_active_agent_from_agent_mail(self, adapter):
        now = datetime.now(timezone.utc).isoformat()
        raw = {
            "agents": [{
                "name": "BlueLake",
                "task": "Fixing auth bug",
                "program": "claude-code",
                "model": "opus-4.6",
                "last_active": now,
                "source": "agent_mail",
            }],
            "source": "agent_mail",
        }
        result = adapter.parse(raw)
        assert result["status"] == "active"
        assert result["current_task"] == "Fixing auth bug"
        assert result["active_count"] == 1
        assert result["data_source"] == "agent_mail"

    def test_idle_agent(self, adapter):
        fifteen_min_ago = (
            datetime.now(timezone.utc) - timedelta(minutes=15)
        ).isoformat()
        raw = {
            "agents": [{
                "name": "RedGlacier",
                "task": "Worker session",
                "program": "claude-code",
                "model": "opus-4.6",
                "last_active": fifteen_min_ago,
                "source": "agent_mail",
            }],
            "source": "agent_mail",
        }
        result = adapter.parse(raw)
        assert result["status"] == "idle"
        assert result["idle_count"] == 1
        assert result["active_count"] == 0

    def test_offline_agent(self, adapter):
        one_hour_ago = (
            datetime.now(timezone.utc) - timedelta(hours=1)
        ).isoformat()
        raw = {
            "agents": [{
                "name": "OldAgent",
                "task": "Done",
                "program": "claude-code",
                "model": "",
                "last_active": one_hour_ago,
                "source": "agent_mail",
            }],
            "source": "agent_mail",
        }
        result = adapter.parse(raw)
        assert result["status"] == "offline"

    def test_mixed_statuses(self, adapter):
        now = datetime.now(timezone.utc)
        raw = {
            "agents": [
                {
                    "name": "Active",
                    "task": "Working",
                    "program": "claude-code",
                    "model": "",
                    "last_active": now.isoformat(),
                    "source": "agent_mail",
                },
                {
                    "name": "Idle",
                    "task": "Paused",
                    "program": "claude-code",
                    "model": "",
                    "last_active": (now - timedelta(minutes=15)).isoformat(),
                    "source": "agent_mail",
                },
            ],
            "source": "agent_mail",
        }
        result = adapter.parse(raw)
        assert result["status"] == "active"  # overall = best status
        assert result["active_count"] == 1
        assert result["idle_count"] == 1
        assert result["total_count"] == 2
        assert result["current_task"] == "Working"


class TestPoll:
    @pytest.mark.asyncio
    async def test_poll_agent_mail_success(self, adapter):
        now = datetime.now(timezone.utc).isoformat()
        am_agents = [{
            "name": "TestAgent",
            "task_description": "Testing",
            "program": "claude-code",
            "model": "opus-4.6",
            "last_active_ts": now,
        }]

        async def mock_poll_am():
            return [{
                "name": "TestAgent",
                "task": "Testing",
                "program": "claude-code",
                "model": "opus-4.6",
                "last_active": now,
                "source": "agent_mail",
            }]

        with patch.object(adapter, "_poll_agent_mail", side_effect=mock_poll_am):
            result = await adapter.poll()

        assert result["source"] == "agent_mail"
        assert len(result["agents"]) == 1
        assert result["agents"][0]["name"] == "TestAgent"
        assert result["agents"][0]["task"] == "Testing"

    @pytest.mark.asyncio
    async def test_poll_agent_mail_failure_falls_back(self, adapter):
        """When Agent Mail is unreachable, falls back to process check."""
        with patch("adapters.claude_status.httpx.AsyncClient") as mock_client_cls:
            mock_client = AsyncMock()
            mock_client.__aenter__ = AsyncMock(return_value=mock_client)
            mock_client.__aexit__ = AsyncMock(return_value=False)
            mock_client.get.side_effect = Exception("Connection refused")
            mock_client_cls.return_value = mock_client

            with patch.object(adapter, "_poll_local_processes", return_value=[]):
                result = await adapter.poll()

        assert result["source"] in ("process_check", "none")

    @pytest.mark.asyncio
    async def test_poll_no_url_skips_agent_mail(self, adapter_no_url):
        """When no URL is configured, goes straight to process check."""
        with patch.object(adapter_no_url, "_poll_local_processes", return_value=[]):
            result = await adapter_no_url.poll()

        assert result["source"] in ("process_check", "none")


class TestClassifyStatus:
    def test_process_check_always_active(self):
        assert ClaudeStatusAdapter._classify_status(
            {"source": "process_check"},
        ) == "active"

    def test_no_timestamp_assumes_active(self):
        assert ClaudeStatusAdapter._classify_status(
            {"source": "agent_mail", "last_active": ""},
        ) == "active"

    def test_recent_is_active(self):
        now = datetime.now(timezone.utc).isoformat()
        assert ClaudeStatusAdapter._classify_status(
            {"source": "agent_mail", "last_active": now},
        ) == "active"

    def test_stale_is_idle(self):
        fifteen_min_ago = (
            datetime.now(timezone.utc) - timedelta(minutes=15)
        ).isoformat()
        assert ClaudeStatusAdapter._classify_status(
            {"source": "agent_mail", "last_active": fifteen_min_ago},
        ) == "idle"

    def test_old_is_offline(self):
        two_hours_ago = (
            datetime.now(timezone.utc) - timedelta(hours=2)
        ).isoformat()
        assert ClaudeStatusAdapter._classify_status(
            {"source": "agent_mail", "last_active": two_hours_ago},
        ) == "offline"
