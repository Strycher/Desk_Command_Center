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
