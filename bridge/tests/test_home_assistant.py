"""Tests for Home Assistant adapter — parsing and poll-once with mocked data."""

from unittest.mock import AsyncMock

import pytest

from adapters import AdapterScheduler, AdapterStatus, TTLCache
from adapters.home_assistant import HomeAssistantAdapter


HA_CONFIG = {
    "home_assistant": {
        "url": "http://192.168.50.24:8123",
        "api_key": "test_token",
        "poll_interval": 60,
    },
}

SAMPLE_RAW = {
    "states": [
        {
            "entity_id": "climate.living_room",
            "state": "heat",
            "attributes": {
                "friendly_name": "Living Room Thermostat",
                "current_temperature": 68.5,
                "temperature": 70,
                "hvac_action": "heating",
                "preset_mode": "home",
            },
            "last_changed": "2025-11-14T10:00:00Z",
        },
        {
            "entity_id": "sensor.outdoor_temp",
            "state": "42.3",
            "attributes": {
                "friendly_name": "Outdoor Temperature",
                "unit_of_measurement": "°F",
                "device_class": "temperature",
            },
            "last_changed": "2025-11-14T09:50:00Z",
        },
        {
            "entity_id": "light.office",
            "state": "on",
            "attributes": {"friendly_name": "Office Light"},
            "last_changed": "2025-11-14T08:00:00Z",
        },
        {
            "entity_id": "person.john",
            "state": "home",
            "attributes": {
                "friendly_name": "John",
                "source": "device_tracker.phone",
            },
            "last_changed": "2025-11-14T07:00:00Z",
        },
        {
            "entity_id": "media_player.tv",
            "state": "playing",
            "attributes": {
                "friendly_name": "Living Room TV",
                "media_title": "Documentary",
                "app_name": "Netflix",
            },
            "last_changed": "2025-11-14T10:30:00Z",
        },
        # Should be filtered out — not in DISPLAY_DOMAINS
        {
            "entity_id": "automation.morning_lights",
            "state": "on",
            "attributes": {"friendly_name": "Morning Lights"},
            "last_changed": "2025-11-14T06:00:00Z",
        },
    ],
}


class TestHomeAssistantAdapter:
    def test_init(self):
        adapter = HomeAssistantAdapter(HA_CONFIG)
        assert adapter.name == "home_assistant"
        assert adapter.url == "http://192.168.50.24:8123"
        assert adapter.token == "test_token"
        assert adapter.config.poll_interval == 60

    def test_init_no_config(self):
        adapter = HomeAssistantAdapter({})
        assert adapter.url == ""
        assert adapter.token == ""

    def test_parse_groups_by_domain(self):
        adapter = HomeAssistantAdapter(HA_CONFIG)
        parsed = adapter.parse(SAMPLE_RAW)

        assert parsed["total_entities"] == 5  # automation filtered out
        domains = parsed["domains"]
        assert "climate" in domains
        assert "sensor" in domains
        assert "light" in domains
        assert "person" in domains
        assert "media_player" in domains
        assert "automation" not in domains

    def test_parse_climate_attrs(self):
        adapter = HomeAssistantAdapter(HA_CONFIG)
        parsed = adapter.parse(SAMPLE_RAW)

        climate = parsed["domains"]["climate"][0]
        assert climate["friendly_name"] == "Living Room Thermostat"
        assert climate["current_temp"] == 68.5
        assert climate["target_temp"] == 70
        assert climate["hvac_action"] == "heating"

    def test_parse_sensor_attrs(self):
        adapter = HomeAssistantAdapter(HA_CONFIG)
        parsed = adapter.parse(SAMPLE_RAW)

        sensor = parsed["domains"]["sensor"][0]
        assert sensor["unit"] == "°F"
        assert sensor["device_class"] == "temperature"

    def test_parse_media_player_attrs(self):
        adapter = HomeAssistantAdapter(HA_CONFIG)
        parsed = adapter.parse(SAMPLE_RAW)

        mp = parsed["domains"]["media_player"][0]
        assert mp["media_title"] == "Documentary"
        assert mp["app_name"] == "Netflix"

    def test_parse_empty(self):
        adapter = HomeAssistantAdapter(HA_CONFIG)
        parsed = adapter.parse({"states": []})
        assert parsed["total_entities"] == 0
        assert parsed["domains"] == {}

    @pytest.mark.asyncio
    async def test_poll_requires_credentials(self):
        adapter = HomeAssistantAdapter({})
        with pytest.raises(ValueError, match="URL and API key"):
            await adapter.poll()

    @pytest.mark.asyncio
    async def test_poll_once_with_mock(self):
        adapter = HomeAssistantAdapter(HA_CONFIG)
        cache = TTLCache()
        sched = AdapterScheduler(cache=cache)
        sched.register(adapter)

        adapter.poll = AsyncMock(return_value=SAMPLE_RAW)

        result = await sched.poll_once(adapter)
        assert result is not None
        assert "climate" in result["domains"]
        assert adapter.state.status == AdapterStatus.OK
        assert cache.get("home_assistant") is not None
