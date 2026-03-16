"""Smoke tests for bridge API endpoints."""

import json
from unittest.mock import patch

from fastapi.testclient import TestClient

from config import BridgeConfig
from main import app, cache

client = TestClient(app)


def setup_function():
    """Clear cache between tests so they don't leak state."""
    cache.clear()


def test_health_returns_ok():
    resp = client.get("/api/health")
    assert resp.status_code == 200
    data = resp.json()
    assert data["status"] == "ok"
    assert data["service"] == "dcc-bridge"
    assert data["version"] == "0.2.0"


def test_adapters_endpoint():
    resp = client.get("/api/adapters")
    assert resp.status_code == 200
    assert "adapters" in resp.json()


def test_calendar_ms_empty():
    resp = client.get("/calendar/ms")
    assert resp.status_code == 404


def test_calendar_ms_ingest_and_retrieve():
    payload = {"count": 1, "events": [{"subject": "Test"}]}
    resp = client.post("/calendar/ms", json=payload)
    assert resp.status_code == 200
    assert resp.json()["accepted"] is True

    resp = client.get("/calendar/ms")
    assert resp.status_code == 200
    assert resp.json()["count"] == 1


def test_calendar_google_empty():
    resp = client.get("/calendar/google")
    assert resp.status_code == 404


def test_calendar_google_ingest_and_retrieve():
    payload = {"count": 2, "events": [{"summary": "A"}, {"summary": "B"}]}
    resp = client.post("/calendar/google", json=payload)
    assert resp.status_code == 200
    assert resp.json()["event_count"] == 2

    resp = client.get("/calendar/google")
    assert resp.status_code == 200
    assert resp.json()["count"] == 2


# --- Dashboard (legacy mode — no device headers) ----------------------------

def test_dashboard_empty():
    resp = client.get("/api/dashboard")
    assert resp.status_code == 200
    data = resp.json()
    assert "generated_at" in data
    assert "sources" in data
    # All sources should be "missing" with no data
    for source in data["sources"].values():
        assert source["status"] == "missing"
        assert source["data"] is None


def test_dashboard_with_ms_calendar():
    payload = {"count": 1, "events": [{"subject": "Standup"}]}
    client.post("/calendar/ms", json=payload)

    resp = client.get("/api/dashboard")
    data = resp.json()
    ms = data["sources"]["calendar_ms"]
    assert ms["status"] == "ok"
    assert ms["data"]["count"] == 1
    # Google calendar should still be missing
    google = data["sources"]["calendar_google"]
    assert google["status"] == "missing"


def test_dashboard_with_both_calendars():
    client.post("/calendar/ms", json={"count": 1, "events": [{"subject": "A"}]})
    client.post("/calendar/google", json={"count": 2, "events": [{"summary": "B"}, {"summary": "C"}]})

    resp = client.get("/api/dashboard")
    data = resp.json()
    assert data["sources"]["calendar_ms"]["status"] == "ok"
    assert data["sources"]["calendar_google"]["status"] == "ok"
    assert data["sources"]["calendar_google"]["data"]["count"] == 2


# --- Dashboard (device-filtered) -------------------------------------------

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
                "github": {"api_key": "gh", "repos": [], "poll_interval": 300},
            },
        },
        "wife": {
            "display_name": "Wife",
            "sources": {
                "weather": {"api_key": "w2", "lat": 39.1, "lon": -84.5, "poll_interval": 900},
            },
        },
    },
    "devices": {
        "AABBCCDDEEFF": {
            "name": "dcc-main", "user": "strycher",
            "key": "secret-key-abc", "registered_at": "2026-03-16T00:00:00Z",
        },
        "112233445566": {
            "name": "dcc-wife", "user": "wife",
            "key": "secret-key-xyz", "registered_at": "2026-03-16T00:00:00Z",
        },
    },
    "unregistered_devices": {},
}


def _mock_config(tmp_path=None):
    """Create a BridgeConfig from MULTI_USER_CONFIG without touching disk."""
    from pathlib import Path
    import tempfile
    cfg = BridgeConfig.__new__(BridgeConfig)
    cfg._data = json.loads(json.dumps(MULTI_USER_CONFIG))
    # Use a temp file so _save() works for record_unregistered_device
    cfg._path = Path(tempfile.mktemp(suffix=".json"))
    return cfg


class TestDeviceFiltering:
    def test_known_device_valid_key(self):
        with patch("main.config", _mock_config()):
            resp = client.get("/api/dashboard", headers={
                "X-Device-ID": "AABBCCDDEEFF",
                "X-Device-Key": "secret-key-abc",
            })
        assert resp.status_code == 200
        sources = resp.json()["sources"]
        # strycher has weather and github + shared HA + push sources
        assert "weather" in sources
        assert "github" in sources
        assert "home_assistant" in sources
        assert "calendar_ms" in sources  # push source always included

    def test_known_device_wrong_key(self):
        with patch("main.config", _mock_config()):
            resp = client.get("/api/dashboard", headers={
                "X-Device-ID": "AABBCCDDEEFF",
                "X-Device-Key": "wrong-key",
            })
        assert resp.status_code == 403

    def test_unknown_device_gets_shared_only(self):
        mock_cfg = _mock_config()
        with patch("main.config", mock_cfg):
            resp = client.get("/api/dashboard", headers={
                "X-Device-ID": "DEADBEEF0000",
            })
        assert resp.status_code == 200
        sources = resp.json()["sources"]
        # Only shared sources + push sources
        assert "home_assistant" in sources
        assert "calendar_ms" in sources
        # Per-user sources should NOT be present
        assert "weather" not in sources
        assert "github" not in sources

    def test_wife_device_filtered_sources(self):
        with patch("main.config", _mock_config()):
            resp = client.get("/api/dashboard", headers={
                "X-Device-ID": "112233445566",
                "X-Device-Key": "secret-key-xyz",
            })
        assert resp.status_code == 200
        sources = resp.json()["sources"]
        # Wife has weather + shared HA
        assert "weather" in sources
        assert "home_assistant" in sources
        # Wife does NOT have github
        assert "github" not in sources

    def test_no_device_header_legacy_mode(self):
        """Without device headers, all sources are returned (backward compat)."""
        resp = client.get("/api/dashboard")
        assert resp.status_code == 200
        # Should include push sources at minimum
        assert "calendar_ms" in resp.json()["sources"]
