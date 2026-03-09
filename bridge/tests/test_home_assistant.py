"""Tests for Home Assistant adapter — parsing and poll-once with mocked data."""

from unittest.mock import AsyncMock, MagicMock, patch

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

    @pytest.mark.asyncio
    async def test_call_service_light_toggle(self):
        adapter = HomeAssistantAdapter(HA_CONFIG)
        mock_response = AsyncMock()
        mock_response.status_code = 200
        mock_response.json.return_value = []
        mock_response.raise_for_status = lambda: None

        with patch("httpx.AsyncClient.post", return_value=mock_response) as mock_post:
            result = await adapter.call_service("light.office", "turn_off")
            assert result is True
            mock_post.assert_called_once()
            call_url = mock_post.call_args[0][0]
            assert "/api/services/light/turn_off" in call_url

    @pytest.mark.asyncio
    async def test_call_service_climate_set_temp(self):
        adapter = HomeAssistantAdapter(HA_CONFIG)
        mock_response = AsyncMock()
        mock_response.status_code = 200
        mock_response.json.return_value = []
        mock_response.raise_for_status = lambda: None

        with patch("httpx.AsyncClient.post", return_value=mock_response) as mock_post:
            result = await adapter.call_service(
                "climate.living_room", "set_temperature",
                {"temperature": 72}
            )
            assert result is True
            call_kwargs = mock_post.call_args
            body = call_kwargs[1].get("json", {}) if call_kwargs[1] else {}
            assert body.get("temperature") == 72

    @pytest.mark.asyncio
    async def test_get_entity_state(self):
        adapter = HomeAssistantAdapter(HA_CONFIG)
        mock_response = MagicMock()
        mock_response.status_code = 200
        mock_response.json.return_value = {
            "entity_id": "light.office",
            "state": "off",
            "attributes": {"friendly_name": "Office Light"},
        }
        mock_response.raise_for_status = lambda: None

        with patch("httpx.AsyncClient.get", return_value=mock_response):
            entity = await adapter.get_entity_state("light.office")
            assert entity["entity_id"] == "light.office"
            assert entity["state"] == "off"
            assert entity["domain"] == "light"
