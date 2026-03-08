"""Tests for weather adapter — OWM and NWS parsing."""

from unittest.mock import AsyncMock, patch

import pytest

from adapters import AdapterScheduler, TTLCache
from adapters.weather import WeatherAdapter


# --- Fixtures ---------------------------------------------------------------

OWM_CONFIG = {
    "weather": {
        "provider": "openweathermap",
        "api_key": "test-key",
        "lat": 39.3451,
        "lon": -84.3916,
        "units": "imperial",
        "poll_interval": 900,
    },
}

NWS_CONFIG = {
    "weather": {
        "provider": "nws",
        "api_key": "",
        "lat": 39.3451,
        "lon": -84.3916,
        "units": "imperial",
        "poll_interval": 900,
    },
}

OWM_CURRENT = {
    "name": "Liberty Township",
    "main": {"temp": 72.5, "feels_like": 70.0, "humidity": 55},
    "weather": [{"main": "Clear", "description": "clear sky", "icon": "01d"}],
    "wind": {"speed": 5.2},
}

OWM_FORECAST = {
    "list": [
        {"dt": 1700000000, "main": {"temp": 68.0}, "weather": [{"main": "Clouds", "description": "overcast clouds", "icon": "03d"}]},
        {"dt": 1700010800, "main": {"temp": 65.0}, "weather": [{"main": "Rain", "description": "light rain", "icon": "10d"}]},
        {"dt": 1700021600, "main": {"temp": 74.0}, "weather": [{"main": "Clear", "description": "clear sky", "icon": "01d"}]},
    ],
}

NWS_OBSERVATION = {
    "properties": {
        "temperature": {"value": 22.5},  # Celsius
        "relativeHumidity": {"value": 55.0},
        "windSpeed": {"value": 16.0},  # km/h
        "textDescription": "Partly Cloudy",
        "icon": "https://api.weather.gov/icons/land/day/sct",
    },
}

NWS_POINT = {
    "properties": {
        "forecast": "https://api.weather.gov/gridpoints/ILN/50,82/forecast",
        "forecastHourly": "https://api.weather.gov/gridpoints/ILN/50,82/forecast/hourly",
        "observationStations": "https://api.weather.gov/gridpoints/ILN/50,82/stations",
        "relativeLocation": {
            "properties": {"city": "Liberty Township", "state": "OH"},
        },
    },
}

NWS_FORECAST = {
    "properties": {
        "periods": [
            {"startTime": "2025-11-14T14:00:00-05:00", "temperature": 72, "temperatureUnit": "F", "shortForecast": "Partly Cloudy", "icon": ""},
            {"startTime": "2025-11-14T15:00:00-05:00", "temperature": 68, "temperatureUnit": "F", "shortForecast": "Cloudy", "icon": ""},
        ],
    },
}


# --- OWM Tests ---------------------------------------------------------------

class TestWeatherAdapterOWM:
    def test_init(self):
        adapter = WeatherAdapter(OWM_CONFIG)
        assert adapter.name == "weather"
        assert adapter.provider == "openweathermap"
        assert adapter.api_key == "test-key"

    def test_parse_owm(self):
        adapter = WeatherAdapter(OWM_CONFIG)
        raw = {"_provider": "owm", "current": OWM_CURRENT, "forecast": OWM_FORECAST}
        parsed = adapter.parse(raw)

        assert parsed["provider"] == "openweathermap"
        assert parsed["location"] == "Liberty Township"
        assert parsed["current"]["temp"] == 72.5
        assert parsed["current"]["condition"] == "Clear Sky"
        assert parsed["temp_unit"] == "F"
        assert parsed["high"] == 74.0
        assert parsed["low"] == 65.0
        assert len(parsed["hourly"]) == 3

    @pytest.mark.asyncio
    async def test_poll_once_with_mock(self):
        adapter = WeatherAdapter(OWM_CONFIG)
        cache = TTLCache()
        sched = AdapterScheduler(cache=cache)
        sched.register(adapter)

        raw = {"_provider": "owm", "current": OWM_CURRENT, "forecast": OWM_FORECAST}
        adapter.poll = AsyncMock(return_value=raw)

        result = await sched.poll_once(adapter)
        assert result is not None
        assert result["provider"] == "openweathermap"
        assert cache.get("weather") is not None


# --- NWS Tests ---------------------------------------------------------------

class TestWeatherAdapterNWS:
    def test_init_nws(self):
        adapter = WeatherAdapter(NWS_CONFIG)
        assert adapter.provider == "nws"

    def test_parse_nws(self):
        adapter = WeatherAdapter(NWS_CONFIG)
        raw = {
            "_provider": "nws",
            "observation": NWS_OBSERVATION,
            "forecast": NWS_FORECAST,
            "point": NWS_POINT,
        }
        parsed = adapter.parse(raw)

        assert parsed["provider"] == "nws"
        assert parsed["location"] == "Liberty Township, OH"
        # 22.5°C -> 72.5°F
        assert parsed["current"]["temp"] == 72.5
        assert parsed["current"]["condition"] == "Partly Cloudy"
        assert parsed["temp_unit"] == "F"
        # Wind: 16 km/h -> ~9.9 mph
        assert parsed["current"]["wind_speed"] == pytest.approx(9.9, abs=0.1)
        assert len(parsed["hourly"]) == 2

    def test_parse_nws_metric(self):
        cfg = {**NWS_CONFIG, "weather": {**NWS_CONFIG["weather"], "units": "metric"}}
        adapter = WeatherAdapter(cfg)
        raw = {
            "_provider": "nws",
            "observation": NWS_OBSERVATION,
            "forecast": NWS_FORECAST,
            "point": NWS_POINT,
        }
        parsed = adapter.parse(raw)
        assert parsed["current"]["temp"] == 22.5
        assert parsed["temp_unit"] == "C"
