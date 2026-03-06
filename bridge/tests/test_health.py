"""Smoke tests for bridge API endpoints."""

from fastapi.testclient import TestClient

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


# --- Dashboard ---------------------------------------------------------------

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
