"""Tests for bridge configuration system."""

import json
from pathlib import Path

from fastapi.testclient import TestClient

from config import BridgeConfig, _migrate_flat_to_nested
from main import app, cache, config


client = TestClient(app)


def setup_function():
    cache.clear()


# --- Legacy flat config (unchanged from before) ---

LEGACY_FLAT_CONFIG = {
    "bridge": {"push_ttl": 600},
    "display": {"poll_interval": 30, "timezone": "America/New_York"},
    "weather": {
        "provider": "openweathermap", "api_key": "sk-weather",
        "lat": 0.0, "lon": 0.0, "units": "imperial",
        "poll_interval": 900,
    },
    "home_assistant": {
        "url": "http://192.168.50.24:8123", "api_key": "ha-token",
        "poll_interval": 60,
    },
    "google_calendar": {
        "client_id": "gcal-id", "client_secret": "gcal-secret",
        "refresh_token": "gcal-tok", "calendars": ["primary"],
        "poll_interval": 300,
    },
    "github": {
        "api_key": "gh-pat", "repos": ["Strycher/DCC"],
        "org_api_key": "", "org_repos": [], "poll_interval": 300,
    },
    "beads": {"host": "dcc-dolt", "port": 3306, "projects": [], "poll_interval": 120},
    "unfocused_tasks": {
        "api_url": "https://getunfocused.app", "api_key": "uf-key",
        "statuses": ["not_started"], "limit": 50, "poll_interval": 300,
    },
}

# --- Nested (multi-user) config ---

NESTED_CONFIG = {
    "bridge": {"push_ttl": 600},
    "display": {"poll_interval": 30, "timezone": "America/New_York"},
    "shared_sources": {
        "home_assistant": {
            "url": "http://192.168.50.24:8123", "api_key": "ha-token",
            "poll_interval": 60,
        },
    },
    "users": {
        "strycher": {
            "display_name": "Chris",
            "sources": {
                "weather": {"api_key": "sk-weather", "lat": 39.1, "lon": -84.5, "poll_interval": 600},
                "github": {"api_key": "gh-pat", "repos": ["Strycher/DCC"], "poll_interval": 300},
            },
        },
        "wife": {
            "display_name": "Wife",
            "sources": {
                "weather": {"api_key": "sk-weather", "lat": 39.1, "lon": -84.5, "poll_interval": 600},
                "google_calendar": {"client_id": "gc", "client_secret": "gs", "refresh_token": "gt"},
            },
        },
    },
    "devices": {
        "A1B2C3D4E5F6": {
            "name": "dcc-7inch", "user": "strycher",
            "key": "tok-abc", "registered_at": "2026-03-16T00:00:00Z",
        },
    },
    "unregistered_devices": {},
}


class TestBridgeConfig:
    def test_defaults_loaded(self, tmp_path):
        cfg = BridgeConfig(path=tmp_path / "test.json")
        data = cfg.get_all(mask_secrets=False)
        assert "shared_sources" in data
        assert "users" in data
        assert cfg.is_multi_user

    def test_secrets_masked(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        masked = cfg.get_all(mask_secrets=True)
        assert masked["shared_sources"]["home_assistant"]["api_key"] == "***"
        assert masked["users"]["strycher"]["sources"]["weather"]["api_key"] == "***"

    def test_empty_secret_not_masked(self, tmp_path):
        cfg = BridgeConfig(path=tmp_path / "test.json")
        masked = cfg.get_all(mask_secrets=True)
        # Empty string secrets show as empty, not ***
        ha = masked["shared_sources"]["home_assistant"]
        assert ha["api_key"] == ""

    def test_update_bridge_section(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        result = cfg.update({"bridge": {"push_ttl": 300}})
        assert "bridge.push_ttl" in result["updated"]
        assert not result["errors"]
        assert cfg.get_all(mask_secrets=False)["bridge"]["push_ttl"] == 300

    def test_update_rejects_users_section(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        result = cfg.update({"users": {"strycher": {}}})
        assert not result["updated"]
        assert any("cannot be updated via REST" in e for e in result["errors"])

    def test_update_unknown_section(self, tmp_path):
        cfg = BridgeConfig(path=tmp_path / "test.json")
        result = cfg.update({"nonexistent": {"key": "val"}})
        assert not result["updated"]
        assert any("cannot be updated" in e for e in result["errors"])

    def test_update_unknown_key(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        result = cfg.update({"bridge": {"nonexistent_key": "val"}})
        assert not result["updated"]
        assert any("Unknown key" in e for e in result["errors"])

    def test_update_type_mismatch(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        result = cfg.update({"bridge": {"push_ttl": "not-a-number"}})
        assert not result["updated"]
        assert any("Type mismatch" in e for e in result["errors"])

    def test_update_persists_to_file(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        cfg.update({"bridge": {"push_ttl": 300}})
        cfg2 = BridgeConfig(path=path)
        assert cfg2.get_all(mask_secrets=False)["bridge"]["push_ttl"] == 300

    def test_int_float_coercion(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        result = cfg.update({"bridge": {"push_ttl": 1200}})
        assert "bridge.push_ttl" in result["updated"]


class TestLegacyMigration:
    def test_flat_config_migrated_on_load(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(LEGACY_FLAT_CONFIG))
        cfg = BridgeConfig(path=path)
        data = cfg.get_all(mask_secrets=False)
        assert "users" in data
        assert "shared_sources" in data
        assert "strycher" in data["users"]
        assert cfg.is_multi_user

    def test_migrated_user_has_sources(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(LEGACY_FLAT_CONFIG))
        cfg = BridgeConfig(path=path)
        sources = cfg.get_user_sources("strycher")
        assert "weather" in sources
        assert "github" in sources
        assert "unfocused_tasks" in sources
        assert sources["weather"]["api_key"] == "sk-weather"

    def test_migrated_shared_sources(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(LEGACY_FLAT_CONFIG))
        cfg = BridgeConfig(path=path)
        shared = cfg.get_shared_sources()
        assert "home_assistant" in shared
        assert shared["home_assistant"]["url"] == "http://192.168.50.24:8123"

    def test_migrated_preserves_bridge_section(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(LEGACY_FLAT_CONFIG))
        cfg = BridgeConfig(path=path)
        assert cfg.get_section("bridge")["push_ttl"] == 600

    def test_ha_not_in_user_sources(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(LEGACY_FLAT_CONFIG))
        cfg = BridgeConfig(path=path)
        sources = cfg.get_user_sources("strycher")
        assert "home_assistant" not in sources

    def test_migrate_function_directly(self):
        result = _migrate_flat_to_nested(LEGACY_FLAT_CONFIG)
        assert "users" in result
        assert "shared_sources" in result
        assert "weather" not in result  # moved into user sources
        assert "home_assistant" not in result  # moved into shared_sources


class TestMultiUserAccessors:
    def test_get_user(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        user = cfg.get_user("strycher")
        assert user is not None
        assert user["display_name"] == "Chris"

    def test_get_user_not_found(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        assert cfg.get_user("nobody") is None

    def test_get_user_sources(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        sources = cfg.get_user_sources("wife")
        assert "weather" in sources
        assert "google_calendar" in sources
        assert "github" not in sources  # wife doesn't have github

    def test_get_user_source(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        weather = cfg.get_user_source("strycher", "weather")
        assert weather is not None
        assert weather["lat"] == 39.1

    def test_list_users(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        users = cfg.list_users()
        assert set(users) == {"strycher", "wife"}

    def test_get_device(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        device = cfg.get_device("A1B2C3D4E5F6")
        assert device is not None
        assert device["user"] == "strycher"
        assert device["key"] == "tok-abc"

    def test_get_device_not_found(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        assert cfg.get_device("DEADBEEF1234") is None

    def test_resolve_device_user(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        assert cfg.resolve_device_user("A1B2C3D4E5F6") == "strycher"
        assert cfg.resolve_device_user("UNKNOWN") is None

    def test_cache_keys_for_user(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        keys = cfg.get_cache_keys_for_user("strycher")
        assert keys["weather"] == "weather:strycher"
        assert keys["github"] == "github:strycher"
        assert keys["home_assistant"] == "home_assistant"  # shared

    def test_cache_keys_for_wife(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        keys = cfg.get_cache_keys_for_user("wife")
        assert keys["weather"] == "weather:wife"
        assert keys["google_calendar"] == "google_calendar:wife"
        assert keys["home_assistant"] == "home_assistant"
        assert "github" not in keys

    def test_cache_keys_shared_only(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        keys = cfg.get_cache_keys_shared_only()
        assert keys == {"home_assistant": "home_assistant"}

    def test_record_unregistered_device(self, tmp_path):
        path = tmp_path / "test.json"
        path.write_text(json.dumps(NESTED_CONFIG))
        cfg = BridgeConfig(path=path)
        cfg.record_unregistered_device("DEADBEEF1234", "192.168.50.99")
        unreg = cfg.get_section("unregistered_devices")
        assert "DEADBEEF1234" in unreg
        assert unreg["DEADBEEF1234"]["source_ip"] == "192.168.50.99"
        # Persisted to disk
        cfg2 = BridgeConfig(path=path)
        assert "DEADBEEF1234" in cfg2.get_section("unregistered_devices")


# --- API endpoint tests ---

def test_get_config_endpoint():
    resp = client.get("/api/config")
    assert resp.status_code == 200
    data = resp.json()
    assert "shared_sources" in data or "bridge" in data

def test_put_config_valid():
    resp = client.put("/api/config", json={"bridge": {"push_ttl": 300}})
    assert resp.status_code == 200
    assert "bridge.push_ttl" in resp.json()["updated"]


def test_put_config_invalid_section():
    resp = client.put("/api/config", json={"users": {"strycher": {}}})
    assert resp.status_code == 400
    assert resp.json()["errors"]


# --- Env-placeholder resolution (issue #252) ---

import os
from config import _resolve_env_placeholders


class TestEnvPlaceholders:
    def test_resolves_string_placeholder(self, monkeypatch):
        monkeypatch.setenv("DCC_TEST_TOKEN", "actual-secret-value")
        assert _resolve_env_placeholders("${ENV:DCC_TEST_TOKEN}") == "actual-secret-value"

    def test_missing_env_resolves_to_empty(self, monkeypatch):
        monkeypatch.delenv("DCC_NEVER_SET_VAR", raising=False)
        assert _resolve_env_placeholders("${ENV:DCC_NEVER_SET_VAR}") == ""

    def test_partial_substitution_in_string(self, monkeypatch):
        monkeypatch.setenv("DCC_TEST_HOST", "example.com")
        result = _resolve_env_placeholders("https://${ENV:DCC_TEST_HOST}/api")
        assert result == "https://example.com/api"

    def test_recursive_dict(self, monkeypatch):
        monkeypatch.setenv("DCC_TEST_SECRET", "abc123")
        node = {"users": {"strycher": {"sources": {"github": {"api_key": "${ENV:DCC_TEST_SECRET}"}}}}}
        out = _resolve_env_placeholders(node)
        assert out["users"]["strycher"]["sources"]["github"]["api_key"] == "abc123"

    def test_non_secret_strings_pass_through(self):
        node = {"poll_interval": 60, "url": "http://localhost:8080", "calendars": ["primary"]}
        out = _resolve_env_placeholders(node)
        assert out == node

    def test_save_does_not_persist_resolved_secrets(self, tmp_path, monkeypatch):
        """Critical: mutations + _save() must write placeholders, not resolved values."""
        monkeypatch.setenv("DCC_HA_TOKEN", "real-ha-token-do-not-leak")
        cfg_path = tmp_path / "test.json"
        cfg_path.write_text(json.dumps({
            "bridge": {"push_ttl": 600},
            "display": {"poll_interval": 30, "timezone": "America/New_York"},
            "shared_sources": {
                "home_assistant": {
                    "url": "http://localhost:8123",
                    "api_key": "${ENV:DCC_HA_TOKEN}",
                    "poll_interval": 60,
                },
            },
            "users": {}, "devices": {}, "unregistered_devices": {},
        }))
        cfg = BridgeConfig(path=cfg_path)
        # Runtime view sees resolved secret
        assert cfg.get_shared_sources()["home_assistant"]["api_key"] == "real-ha-token-do-not-leak"
        # Mutate something else and save
        cfg.update({"bridge": {"push_ttl": 300}})
        # On-disk file must still have the placeholder, not the resolved secret
        on_disk = json.loads(cfg_path.read_text())
        assert on_disk["shared_sources"]["home_assistant"]["api_key"] == "${ENV:DCC_HA_TOKEN}"
        assert "real-ha-token-do-not-leak" not in cfg_path.read_text()

    def test_record_unregistered_device_preserves_placeholders(self, tmp_path, monkeypatch):
        monkeypatch.setenv("DCC_HA_TOKEN", "another-secret")
        cfg_path = tmp_path / "test.json"
        cfg_path.write_text(json.dumps({
            "bridge": {"push_ttl": 600},
            "display": {"poll_interval": 30, "timezone": "America/New_York"},
            "shared_sources": {
                "home_assistant": {
                    "url": "http://localhost:8123",
                    "api_key": "${ENV:DCC_HA_TOKEN}",
                    "poll_interval": 60,
                },
            },
            "users": {}, "devices": {}, "unregistered_devices": {},
        }))
        cfg = BridgeConfig(path=cfg_path)
        cfg.record_unregistered_device("DEADBEEF", "192.168.1.99")
        on_disk = json.loads(cfg_path.read_text())
        assert on_disk["shared_sources"]["home_assistant"]["api_key"] == "${ENV:DCC_HA_TOKEN}"
        assert "DEADBEEF" in on_disk["unregistered_devices"]
