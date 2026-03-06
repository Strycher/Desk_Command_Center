"""Tests for bridge configuration system."""

import json
from pathlib import Path

from fastapi.testclient import TestClient

from config import BridgeConfig
from main import app, cache, config


client = TestClient(app)


def setup_function():
    cache.clear()


class TestBridgeConfig:
    def test_defaults_loaded(self, tmp_path):
        cfg = BridgeConfig(path=tmp_path / "test.json")
        data = cfg.get_all(mask_secrets=False)
        assert "weather" in data
        assert data["weather"]["lat"] == 39.3451

    def test_secrets_masked(self, tmp_path):
        cfg = BridgeConfig(path=tmp_path / "test.json")
        # Set a real API key
        cfg.update({"weather": {"api_key": "sk-secret-123"}})
        masked = cfg.get_all(mask_secrets=True)
        assert masked["weather"]["api_key"] == "***"

    def test_empty_secret_not_masked(self, tmp_path):
        cfg = BridgeConfig(path=tmp_path / "test.json")
        masked = cfg.get_all(mask_secrets=True)
        # Empty string secrets show as empty, not ***
        assert masked["weather"]["api_key"] == ""

    def test_update_valid_key(self, tmp_path):
        cfg = BridgeConfig(path=tmp_path / "test.json")
        result = cfg.update({"weather": {"units": "metric"}})
        assert "weather.units" in result["updated"]
        assert not result["errors"]
        assert cfg.get_all(mask_secrets=False)["weather"]["units"] == "metric"

    def test_update_unknown_section(self, tmp_path):
        cfg = BridgeConfig(path=tmp_path / "test.json")
        result = cfg.update({"nonexistent": {"key": "val"}})
        assert not result["updated"]
        assert any("Unknown section" in e for e in result["errors"])

    def test_update_unknown_key(self, tmp_path):
        cfg = BridgeConfig(path=tmp_path / "test.json")
        result = cfg.update({"weather": {"nonexistent_key": "val"}})
        assert not result["updated"]
        assert any("Unknown key" in e for e in result["errors"])

    def test_update_type_mismatch(self, tmp_path):
        cfg = BridgeConfig(path=tmp_path / "test.json")
        result = cfg.update({"weather": {"lat": "not-a-number"}})
        assert not result["updated"]
        assert any("Type mismatch" in e for e in result["errors"])

    def test_update_persists_to_file(self, tmp_path):
        path = tmp_path / "test.json"
        cfg = BridgeConfig(path=path)
        cfg.update({"weather": {"units": "metric"}})

        # Reload from file
        cfg2 = BridgeConfig(path=path)
        assert cfg2.get_all(mask_secrets=False)["weather"]["units"] == "metric"

    def test_int_float_coercion(self, tmp_path):
        cfg = BridgeConfig(path=tmp_path / "test.json")
        result = cfg.update({"weather": {"poll_interval": 1200}})
        assert "weather.poll_interval" in result["updated"]


# --- API endpoint tests ---

def test_get_config_endpoint():
    resp = client.get("/api/config")
    assert resp.status_code == 200
    data = resp.json()
    assert "weather" in data
    assert "bridge" in data


def test_put_config_valid():
    resp = client.put("/api/config", json={"display": {"poll_interval": 15}})
    assert resp.status_code == 200
    assert "display.poll_interval" in resp.json()["updated"]


def test_put_config_invalid_section():
    resp = client.put("/api/config", json={"bogus": {"key": "val"}})
    assert resp.status_code == 400
    assert resp.json()["errors"]
