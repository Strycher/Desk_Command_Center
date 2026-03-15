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


# Label-mode test data: simulates registry output with area info
LABEL_REGISTRY = {
    "dcc_entity_ids": {
        "climate.hallway", "sensor.hallway_temp", "sensor.hallway_humidity",
        "switch.tv_outlet", "media_player.samsung_tv",
        "light.back_porch", "person.john",
    },
    "entity_info": {
        "climate.hallway": {"device_id": "dev_nest", "area_id": None},
        "sensor.hallway_temp": {"device_id": "dev_nest", "area_id": None},
        "sensor.hallway_humidity": {"device_id": "dev_nest", "area_id": None},
        "switch.tv_outlet": {"device_id": "dev_outlet", "area_id": None},
        "media_player.samsung_tv": {"device_id": "dev_tv", "area_id": None},
        "light.back_porch": {"device_id": "dev_porch", "area_id": None},
        "person.john": {"device_id": None, "area_id": None},
    },
    "device_names": {
        "dev_nest": "Nest Thermostat",
        "dev_outlet": "TV Outlet",
        "dev_tv": "Samsung TV",
        "dev_porch": "Back Porch Lights",
    },
    "device_areas": {
        "dev_nest": "area_hallway",
        "dev_outlet": "area_living",
        "dev_tv": "area_living",
        "dev_porch": "area_porch",
    },
    "area_names": {
        "area_hallway": "Hallway",
        "area_living": "Living Room",
        "area_porch": "Back Porch",
    },
}

LABEL_STATES = [
    {
        "entity_id": "climate.hallway",
        "state": "heat",
        "attributes": {
            "friendly_name": "Hallway",
            "current_temperature": 68,
            "temperature": 70,
            "hvac_action": "heating",
            "preset_mode": "home",
        },
    },
    {
        "entity_id": "sensor.hallway_temp",
        "state": "68",
        "attributes": {
            "friendly_name": "Hallway Temperature",
            "unit_of_measurement": "°F",
            "device_class": "temperature",
        },
    },
    {
        "entity_id": "sensor.hallway_humidity",
        "state": "45",
        "attributes": {
            "friendly_name": "Hallway Humidity",
            "unit_of_measurement": "%",
            "device_class": "humidity",
        },
    },
    {
        "entity_id": "switch.tv_outlet",
        "state": "on",
        "attributes": {"friendly_name": "TV Outlet"},
    },
    {
        "entity_id": "media_player.samsung_tv",
        "state": "off",
        "attributes": {"friendly_name": "Samsung TV"},
    },
    {
        "entity_id": "light.back_porch",
        "state": "on",
        "attributes": {"friendly_name": "Back Porch Lights"},
    },
    {
        "entity_id": "person.john",
        "state": "home",
        "attributes": {"friendly_name": "John", "source": "device_tracker.phone"},
    },
]


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

    # ── Label-mode area tests ──────────────────────────────────

    def test_label_mode_devices_have_area_name(self):
        """Devices in label mode should include area_name from registry."""
        adapter = HomeAssistantAdapter(HA_CONFIG)
        parsed = adapter.parse({
            "states": LABEL_STATES,
            "registry": LABEL_REGISTRY,
        })

        assert parsed["label_mode"] is True
        devices = parsed["devices"]

        # Build lookup by device_name
        by_name = {d["device_name"]: d for d in devices}

        assert by_name["Nest Thermostat"]["area_name"] == "Hallway"
        assert by_name["TV Outlet"]["area_name"] == "Living Room"
        assert by_name["Samsung TV"]["area_name"] == "Living Room"
        assert by_name["Back Porch Lights"]["area_name"] == "Back Porch"

    def test_label_mode_standalone_no_area(self):
        """Standalone entities without area_id should have area_name=None."""
        adapter = HomeAssistantAdapter(HA_CONFIG)
        parsed = adapter.parse({
            "states": LABEL_STATES,
            "registry": LABEL_REGISTRY,
        })

        standalone = parsed["standalone"]
        assert len(standalone) == 1
        assert standalone[0]["entity_id"] == "person.john"
        assert standalone[0]["area_name"] is None

    def test_label_mode_entity_area_override(self):
        """Entity-level area_id should override device-level area_id."""
        adapter = HomeAssistantAdapter(HA_CONFIG)

        # Give the TV outlet entity a direct area override to "Hallway"
        registry = {**LABEL_REGISTRY}
        registry["entity_info"] = {
            **LABEL_REGISTRY["entity_info"],
            "switch.tv_outlet": {
                "device_id": "dev_outlet",
                "area_id": "area_hallway",  # override: entity says Hallway
            },
        }

        parsed = adapter.parse({
            "states": LABEL_STATES,
            "registry": registry,
        })

        by_name = {d["device_name"]: d for d in parsed["devices"]}
        # TV Outlet device should resolve to Hallway (entity override),
        # not Living Room (device-level)
        assert by_name["TV Outlet"]["area_name"] == "Hallway"

    def test_label_mode_device_no_area(self):
        """Device with no area_id should have area_name=None."""
        adapter = HomeAssistantAdapter(HA_CONFIG)

        registry = {**LABEL_REGISTRY}
        registry["device_areas"] = {
            **LABEL_REGISTRY["device_areas"],
            "dev_tv": None,  # Samsung TV has no area
        }

        parsed = adapter.parse({
            "states": LABEL_STATES,
            "registry": registry,
        })

        by_name = {d["device_name"]: d for d in parsed["devices"]}
        assert by_name["Samsung TV"]["area_name"] is None

    def test_resolve_area_name_priority(self):
        """_resolve_area_name should prefer entity area over device area."""
        adapter = HomeAssistantAdapter(HA_CONFIG)

        # Entity area set → should use entity area
        result = adapter._resolve_area_name(
            "dev_nest", "area_living", LABEL_REGISTRY,
        )
        assert result == "Living Room"  # entity override wins

        # Entity area None → should fall back to device area
        result = adapter._resolve_area_name(
            "dev_nest", None, LABEL_REGISTRY,
        )
        assert result == "Hallway"  # device-level area

        # Both None → should return None
        result = adapter._resolve_area_name(None, None, LABEL_REGISTRY)
        assert result is None
