"""Integration tests for multi-user bridge features.

Tests adapter factory, heartbeat user scoping, and end-to-end
multi-user dashboard scenarios.
"""

import json
import tempfile
from pathlib import Path
from unittest.mock import patch

import pytest
from fastapi.testclient import TestClient

from config import BridgeConfig
from main import (
    ADAPTER_CLASSES,
    app,
    cache,
    create_adapter,
    _claude_sessions,
)

client = TestClient(app)

MULTI_USER_CONFIG = {
    "bridge": {"push_ttl": 600},
    "shared_sources": {
        "home_assistant": {"url": "http://ha:8123", "api_key": "tok", "poll_interval": 60},
    },
    "users": {
        "strycher": {
            "display_name": "Chris",
            "sources": {
                "weather": {"api_key": "w1", "lat": 39.1, "lon": -84.5, "poll_interval": 900},
                "github": {"api_key": "gh", "repos": ["Strycher/DCC"], "poll_interval": 300},
                "beads": {"host": "dcc-dolt", "port": 3306, "projects": [], "poll_interval": 120},
                "claude": {},
            },
        },
        "wife": {
            "display_name": "Wife",
            "sources": {
                "weather": {"api_key": "w2", "lat": 39.1, "lon": -84.5, "poll_interval": 900},
                "google_calendar": {
                    "client_id": "gc", "client_secret": "gs",
                    "refresh_token": "gt", "poll_interval": 300,
                },
            },
        },
    },
    "devices": {
        "AABBCCDDEEFF": {
            "name": "dcc-main", "user": "strycher",
            "key": "key-abc", "registered_at": "2026-03-16T00:00:00Z",
        },
        "112233445566": {
            "name": "dcc-wife", "user": "wife",
            "key": "key-xyz", "registered_at": "2026-03-16T00:00:00Z",
        },
    },
    "unregistered_devices": {},
}


def _mock_config():
    cfg = BridgeConfig.__new__(BridgeConfig)
    cfg._data = json.loads(json.dumps(MULTI_USER_CONFIG))
    cfg._path = Path(tempfile.mktemp(suffix=".json"))
    return cfg


def setup_function():
    cache.clear()
    _claude_sessions.clear()


# --- Adapter factory tests ---

class TestAdapterFactory:
    def test_create_weather_adapter(self):
        adapter = create_adapter("weather", {"api_key": "k", "lat": 39.0, "lon": -84.0})
        assert adapter.name == "weather"
        assert adapter.cache_key == "weather"  # no user_id

    def test_create_weather_adapter_per_user(self):
        adapter = create_adapter("weather", {"api_key": "k"}, user_id="strycher")
        assert adapter.name == "weather"
        assert adapter.cache_key == "weather:strycher"

    def test_create_github_adapter(self):
        adapter = create_adapter("github", {"api_key": "gh", "repos": ["a/b"]}, user_id="wife")
        assert adapter.name == "github"
        assert adapter.cache_key == "github:wife"

    def test_create_unknown_source_raises(self):
        with pytest.raises(ValueError, match="Unknown adapter source"):
            create_adapter("nonexistent", {})

    def test_all_adapter_classes_registered(self):
        expected = {"weather", "github", "google_calendar", "beads", "unfocused_tasks", "home_assistant"}
        assert set(ADAPTER_CLASSES.keys()) == expected

    def test_shared_adapter_no_user_scope(self):
        adapter = create_adapter("home_assistant", {"url": "http://ha:8123"})
        assert adapter.cache_key == "home_assistant"  # bare, not scoped


# --- Claude heartbeat user scoping ---

class TestClaudeHeartbeatScoping:
    def setup_method(self):
        cache.clear()
        _claude_sessions.clear()

    def test_heartbeat_legacy_no_device(self):
        resp = client.post("/api/claude/heartbeat", json={
            "agent": "TestAgent", "task": "Testing", "program": "test",
        })
        assert resp.status_code == 200
        data = resp.json()
        assert data["user"] == "claude"  # legacy unscoped
        assert cache.get("claude") is not None

    def test_heartbeat_with_device_header(self):
        with patch("main.config", _mock_config()):
            resp = client.post("/api/claude/heartbeat",
                json={"agent": "Agent1", "task": "Working"},
                headers={"X-Device-ID": "AABBCCDDEEFF"},
            )
        assert resp.status_code == 200
        assert resp.json()["user"] == "strycher"
        assert cache.get("claude:strycher") is not None

    def test_heartbeat_unknown_device_falls_back(self):
        with patch("main.config", _mock_config()):
            resp = client.post("/api/claude/heartbeat",
                json={"agent": "Agent2", "task": "Working"},
                headers={"X-Device-ID": "DEADBEEF0000"},
            )
        assert resp.status_code == 200
        assert resp.json()["user"] == "claude"  # fallback

    def test_goodbye_scoped(self):
        with patch("main.config", _mock_config()):
            client.post("/api/claude/heartbeat",
                json={"agent": "Agent3", "task": "Working"},
                headers={"X-Device-ID": "AABBCCDDEEFF"},
            )
            resp = client.request("DELETE", "/api/claude/heartbeat",
                json={"agent": "Agent3"},
                headers={"X-Device-ID": "AABBCCDDEEFF"},
            )
        assert resp.status_code == 200
        assert resp.json()["user"] == "strycher"

    def test_separate_user_sessions(self):
        mock_cfg = _mock_config()
        with patch("main.config", mock_cfg):
            client.post("/api/claude/heartbeat",
                json={"agent": "AgentA", "task": "DCC work"},
                headers={"X-Device-ID": "AABBCCDDEEFF"},
            )
            client.post("/api/claude/heartbeat",
                json={"agent": "AgentB", "task": "Wife work"},
                headers={"X-Device-ID": "112233445566"},
            )
            strycher_data = cache.get("claude:strycher")
            wife_data = cache.get("claude:wife")
        assert strycher_data is not None
        assert wife_data is not None
        assert strycher_data["sessions"][0]["agent"] == "AgentA"
        assert wife_data["sessions"][0]["agent"] == "AgentB"


# --- End-to-end multi-user dashboard scenario ---

class TestMultiUserDashboard:
    def test_two_devices_different_sources(self):
        """Gate 1 validation: two devices return different source sets."""
        mock_cfg = _mock_config()
        with patch("main.config", mock_cfg):
            # strycher's device
            resp1 = client.get("/api/dashboard", headers={
                "X-Device-ID": "AABBCCDDEEFF",
                "X-Device-Key": "key-abc",
            })
            # wife's device
            resp2 = client.get("/api/dashboard", headers={
                "X-Device-ID": "112233445566",
                "X-Device-Key": "key-xyz",
            })

        s1 = set(resp1.json()["sources"].keys())
        s2 = set(resp2.json()["sources"].keys())

        # strycher has github, beads, claude — wife doesn't
        assert "github" in s1
        assert "beads" in s1
        assert "claude" in s1
        assert "github" not in s2
        assert "beads" not in s2

        # Both have weather and HA
        assert "weather" in s1
        assert "weather" in s2
        assert "home_assistant" in s1
        assert "home_assistant" in s2

        # Wife has google_calendar — strycher doesn't
        assert "google_calendar" in s2
        assert "google_calendar" not in s1

    def test_legacy_mode_returns_all(self):
        """Without device headers, all registered sources are returned."""
        resp = client.get("/api/dashboard")
        assert resp.status_code == 200
        sources = resp.json()["sources"]
        # Push sources always present
        assert "calendar_ms" in sources
        assert "calendar_google" in sources
        assert "claude" in sources
