"""Tests for GitHub adapter — parsing and poll-once with mocked data."""

from unittest.mock import AsyncMock

import pytest

from adapters import AdapterScheduler, AdapterStatus, TTLCache
from adapters.github import GitHubAdapter


GH_CONFIG = {
    "github": {
        "api_key": "ghp_test",
        "repos": ["Strycher/Desk_Command_Center", "DifferentWire/Unfocused"],
        "poll_interval": 300,
    },
}

SAMPLE_RAW = {
    "repos": {
        "Strycher/Desk_Command_Center": {
            "prs": [
                {
                    "number": 42,
                    "title": "feat: add weather widget",
                    "user": {"login": "strycher"},
                    "draft": False,
                    "created_at": "2025-11-14T10:00:00Z",
                    "updated_at": "2025-11-14T12:00:00Z",
                },
            ],
            "issues": [
                {
                    "number": 55,
                    "title": "GitHub adapter",
                    "labels": [{"name": "feature"}],
                    "assignee": {"login": "strycher"},
                    "created_at": "2025-11-14T09:00:00Z",
                },
            ],
            "runs": [
                {
                    "name": "CI — Bridge Build + Test",
                    "status": "completed",
                    "conclusion": "success",
                    "head_branch": "main",
                    "created_at": "2025-11-14T11:00:00Z",
                },
                {
                    "name": "CI — Bridge Build + Test",
                    "status": "completed",
                    "conclusion": "success",
                    "head_branch": "feat/weather",
                    "created_at": "2025-11-14T10:00:00Z",
                },
            ],
        },
        "DifferentWire/Unfocused": {
            "error": "rate limited",
        },
    },
}


class TestGitHubAdapter:
    def test_init(self):
        adapter = GitHubAdapter(GH_CONFIG)
        assert adapter.name == "github"
        assert len(adapter.repos) == 2
        assert adapter.token == "ghp_test"

    def test_init_no_config(self):
        adapter = GitHubAdapter({})
        assert adapter.repos == []
        assert adapter.token == ""

    def test_parse(self):
        adapter = GitHubAdapter(GH_CONFIG)
        parsed = adapter.parse(SAMPLE_RAW)

        dcc = parsed["repos"]["Strycher/Desk_Command_Center"]
        assert dcc["status"] == "ok"
        assert dcc["open_prs"] == 1
        assert dcc["open_issues"] == 1
        assert dcc["prs"][0]["number"] == 42
        assert dcc["issues"][0]["assignee"] == "strycher"
        # CI should deduplicate — only one "CI — Bridge Build + Test"
        assert len(dcc["ci"]) == 1
        assert dcc["ci"][0]["conclusion"] == "success"

    def test_parse_error_repo(self):
        adapter = GitHubAdapter(GH_CONFIG)
        parsed = adapter.parse(SAMPLE_RAW)

        unfocused = parsed["repos"]["DifferentWire/Unfocused"]
        assert unfocused["status"] == "error"
        assert "rate limited" in unfocused["error"]

    def test_parse_empty(self):
        adapter = GitHubAdapter(GH_CONFIG)
        parsed = adapter.parse({"repos": {}})
        assert parsed["repos"] == {}

    @pytest.mark.asyncio
    async def test_poll_no_repos(self):
        adapter = GitHubAdapter({})
        result = await adapter.poll()
        assert result == {"repos": {}}

    @pytest.mark.asyncio
    async def test_poll_once_with_mock(self):
        adapter = GitHubAdapter(GH_CONFIG)
        cache = TTLCache()
        sched = AdapterScheduler(cache=cache)
        sched.register(adapter)

        adapter.poll = AsyncMock(return_value=SAMPLE_RAW)

        result = await sched.poll_once(adapter)
        assert result is not None
        assert "Strycher/Desk_Command_Center" in result["repos"]
        assert adapter.state.status == AdapterStatus.OK
        assert cache.get("github") is not None
