"""Tests for Beads/Dolt adapter — parsing and mock polling."""

from unittest.mock import AsyncMock

import pytest

from adapters import AdapterScheduler, AdapterStatus, TTLCache
from adapters.beads import BeadsAdapter


BEADS_CONFIG = {
    "beads": {
        "host": "127.0.0.1",
        "port": 3310,
        "projects": ["beads_DCC"],
        "poll_interval": 120,
    },
}

SAMPLE_RAW = {
    "projects": {
        "beads_DCC": [
            {
                "id": "Desk_Command_Center-kom",
                "title": "10.8 Beads/Dolt tasks adapter (GH #58)",
                "status": "in_progress",
                "priority": 2,
                "assignee": "Strycher",
                "external_ref": "GH #58",
                "type": "task",
            },
            {
                "id": "Desk_Command_Center-abc",
                "title": "Some open task",
                "status": "open",
                "priority": 1,
                "assignee": None,
                "external_ref": None,
                "type": "feature",
            },
        ],
    },
}

SAMPLE_RAW_ERROR = {
    "projects": {
        "beads_DCC": [{"error": "tunnel down"}],
    },
}


class TestBeadsAdapter:
    def test_init(self):
        adapter = BeadsAdapter(BEADS_CONFIG)
        assert adapter.name == "beads"
        assert adapter.host == "127.0.0.1"
        assert adapter.port == 3310

    def test_init_defaults(self):
        adapter = BeadsAdapter({})
        assert adapter.projects == []
        assert adapter.port == 3310

    def test_parse(self):
        adapter = BeadsAdapter(BEADS_CONFIG)
        parsed = adapter.parse(SAMPLE_RAW)

        assert parsed["total_open"] == 1
        assert parsed["total_in_progress"] == 1

        dcc = parsed["projects"]["beads_DCC"]
        assert dcc["status"] == "ok"
        assert dcc["display_name"] == "DCC"
        assert dcc["open"] == 1
        assert dcc["in_progress"] == 1
        assert len(dcc["tasks"]) == 2

    def test_parse_error(self):
        adapter = BeadsAdapter(BEADS_CONFIG)
        parsed = adapter.parse(SAMPLE_RAW_ERROR)

        dcc = parsed["projects"]["beads_DCC"]
        assert dcc["status"] == "error"
        assert "tunnel down" in dcc["error"]

    def test_parse_empty(self):
        adapter = BeadsAdapter(BEADS_CONFIG)
        parsed = adapter.parse({"projects": {}})
        assert parsed["total_open"] == 0
        assert parsed["total_in_progress"] == 0

    @pytest.mark.asyncio
    async def test_poll_once_with_mock(self):
        adapter = BeadsAdapter(BEADS_CONFIG)
        cache = TTLCache()
        sched = AdapterScheduler(cache=cache)
        sched.register(adapter)

        adapter.poll = AsyncMock(return_value=SAMPLE_RAW)

        result = await sched.poll_once(adapter)
        assert result is not None
        assert result["total_open"] == 1
        assert adapter.state.status == AdapterStatus.OK
        assert cache.get("beads") is not None
