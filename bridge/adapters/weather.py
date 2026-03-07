"""Weather adapter — fetches current + forecast from OpenWeatherMap or NWS."""

from __future__ import annotations

import logging
from collections import defaultdict
from datetime import datetime, timezone
from typing import Any

import httpx

from .base import AdapterConfig, BaseAdapter

logger = logging.getLogger(__name__)

# NWS API requires a contact header
NWS_HEADERS = {
    "User-Agent": "DCC-Bridge/0.2.0 (desk-command-center)",
    "Accept": "application/geo+json",
}


class WeatherAdapter(BaseAdapter):
    """Polls weather data from OpenWeatherMap (with API key) or NWS (free).

    Config is read from BridgeConfig at init:
      weather.provider: "openweathermap" or "nws"
      weather.api_key: OWM API key (required for OWM)
      weather.lat / weather.lon: coordinates
      weather.units: "imperial" or "metric"
      weather.poll_interval: seconds between polls
    """

    def __init__(self, bridge_config: dict[str, Any]) -> None:
        weather_cfg = bridge_config.get("weather", {})
        poll_interval = weather_cfg.get("poll_interval", 900)

        super().__init__(
            name="weather",
            config=AdapterConfig(
                poll_interval=poll_interval,
                ttl=poll_interval * 2,
                max_retries=2,
                backoff_base=5.0,
                backoff_max=120.0,
            ),
        )
        self.provider = weather_cfg.get("provider", "nws")
        self.api_key = weather_cfg.get("api_key", "")
        self.lat = weather_cfg.get("lat", 39.3451)
        self.lon = weather_cfg.get("lon", -84.3916)
        self.units = weather_cfg.get("units", "imperial")

    async def poll(self) -> dict[str, Any]:
        if self.provider == "openweathermap" and self.api_key:
            return await self._poll_owm()
        return await self._poll_nws()

    def parse(self, raw: dict[str, Any]) -> dict[str, Any]:
        provider = raw.get("_provider", "unknown")
        if provider == "owm":
            return self._parse_owm(raw)
        return self._parse_nws(raw)

    # --- OpenWeatherMap ---

    async def _poll_owm(self) -> dict[str, Any]:
        async with httpx.AsyncClient(timeout=10.0) as client:
            current = await client.get(
                "https://api.openweathermap.org/data/2.5/weather",
                params={
                    "lat": self.lat,
                    "lon": self.lon,
                    "appid": self.api_key,
                    "units": self.units,
                },
            )
            current.raise_for_status()

            forecast = await client.get(
                "https://api.openweathermap.org/data/2.5/forecast",
                params={
                    "lat": self.lat,
                    "lon": self.lon,
                    "appid": self.api_key,
                    "units": self.units,
                    "cnt": 40,  # 40 × 3h = 5 days
                },
            )
            forecast.raise_for_status()

        return {
            "_provider": "owm",
            "current": current.json(),
            "forecast": forecast.json(),
        }

    def _parse_owm(self, raw: dict[str, Any]) -> dict[str, Any]:
        cur = raw["current"]
        fc = raw["forecast"]
        temp_unit = "F" if self.units == "imperial" else "C"
        entries = fc.get("list", [])

        # Extract hourly forecast from first 8 × 3-hour blocks (24h)
        hourly = []
        for entry in entries[:8]:
            pop = entry.get("pop", 0)  # 0.0-1.0
            hourly.append({
                "time": entry["dt"],
                "temp": entry["main"]["temp"],
                "condition": entry["weather"][0]["main"],
                "icon": entry["weather"][0]["icon"],
                "precip_chance": round(pop * 100),
            })

        # Aggregate daily forecasts from all entries (up to 5 days)
        day_buckets: dict[str, list[dict]] = defaultdict(list)
        for entry in entries:
            dt_txt = entry.get("dt_txt", "")  # "2026-03-07 15:00:00"
            date_str = dt_txt[:10] if dt_txt else ""
            if date_str:
                day_buckets[date_str].append(entry)

        daily = []
        for date_str in sorted(day_buckets.keys()):
            bucket = day_buckets[date_str]
            temps = [e["main"]["temp"] for e in bucket]
            pops = [e.get("pop", 0) for e in bucket]
            # Pick midday icon (12:00 or closest) for representative condition
            midday = None
            for e in bucket:
                if "12:00:00" in e.get("dt_txt", ""):
                    midday = e
                    break
            if not midday:
                midday = bucket[len(bucket) // 2]
            # Day name from date string
            try:
                day_name = datetime.strptime(date_str, "%Y-%m-%d").strftime("%a")
            except ValueError:
                day_name = date_str
            daily.append({
                "date": date_str,
                "day": day_name,
                "temp_high": round(max(temps), 1),
                "temp_low": round(min(temps), 1),
                "icon": midday["weather"][0]["icon"],
                "condition": midday["weather"][0]["main"],
                "precip_chance": round(max(pops) * 100),
            })

        # Find daily high/low from first 24h forecast + current
        temps_24h = [e["main"]["temp"] for e in entries[:8]]
        temps_24h.append(cur["main"]["temp"])

        return {
            "provider": "openweathermap",
            "location": cur.get("name", "Unknown"),
            "current": {
                "temp": cur["main"]["temp"],
                "feels_like": cur["main"]["feels_like"],
                "humidity": cur["main"]["humidity"],
                "condition": cur["weather"][0]["main"],
                "description": cur["weather"][0]["description"],
                "icon": cur["weather"][0]["icon"],
                "wind_speed": cur.get("wind", {}).get("speed"),
                "precip_chance": round(hourly[0].get("precip_chance", 0)) if hourly else 0,
            },
            "high": max(temps_24h),
            "low": min(temps_24h),
            "temp_unit": temp_unit,
            "hourly": hourly,
            "daily": daily,
        }

    # --- National Weather Service (free, no key) ---

    async def _poll_nws(self) -> dict[str, Any]:
        async with httpx.AsyncClient(timeout=10.0, headers=NWS_HEADERS) as client:
            # Step 1: Get the grid point for this lat/lon
            points = await client.get(
                f"https://api.weather.gov/points/{self.lat},{self.lon}",
            )
            points.raise_for_status()
            point_data = points.json()

            forecast_url = point_data["properties"]["forecast"]
            hourly_url = point_data["properties"]["forecastHourly"]

            # Step 2: Get current observations from nearest station
            station_url = point_data["properties"]["observationStations"]
            stations = await client.get(station_url)
            stations.raise_for_status()
            station_id = stations.json()["features"][0]["properties"]["stationIdentifier"]

            obs = await client.get(
                f"https://api.weather.gov/stations/{station_id}/observations/latest",
            )
            obs.raise_for_status()

            # Step 3: Get forecast
            fc = await client.get(hourly_url)
            fc.raise_for_status()

        return {
            "_provider": "nws",
            "observation": obs.json(),
            "forecast": fc.json(),
            "point": point_data,
        }

    def _parse_nws(self, raw: dict[str, Any]) -> dict[str, Any]:
        obs = raw["observation"]["properties"]
        fc_periods = raw["forecast"]["properties"]["periods"][:12]
        point = raw["point"]["properties"]

        # NWS returns Celsius — convert if imperial
        temp_c = obs.get("temperature", {}).get("value")
        if temp_c is not None and self.units == "imperial":
            temp = round(temp_c * 9 / 5 + 32, 1)
            temp_unit = "F"
        elif temp_c is not None:
            temp = round(temp_c, 1)
            temp_unit = "C"
        else:
            temp = None
            temp_unit = "F" if self.units == "imperial" else "C"

        humidity = obs.get("relativeHumidity", {}).get("value")
        wind_speed_kph = obs.get("windSpeed", {}).get("value")
        wind_speed = None
        if wind_speed_kph is not None:
            wind_speed = round(wind_speed_kph * 0.621371, 1) if self.units == "imperial" else round(wind_speed_kph, 1)

        # Extract hourly forecast temps for high/low
        hourly = []
        forecast_temps = []
        for period in fc_periods:
            period_temp = period["temperature"]
            if self.units == "imperial" and period["temperatureUnit"] == "F":
                forecast_temps.append(period_temp)
            elif self.units == "metric" and period["temperatureUnit"] == "C":
                forecast_temps.append(period_temp)
            hourly.append({
                "time": period["startTime"],
                "temp": period_temp,
                "condition": period["shortForecast"],
                "icon": period.get("icon", ""),
            })

        if temp is not None:
            forecast_temps.append(temp)

        return {
            "provider": "nws",
            "location": f"{point.get('relativeLocation', {}).get('properties', {}).get('city', 'Unknown')}, "
                        f"{point.get('relativeLocation', {}).get('properties', {}).get('state', '')}",
            "current": {
                "temp": temp,
                "feels_like": None,  # NWS doesn't provide feels-like in observations
                "humidity": round(humidity, 1) if humidity is not None else None,
                "condition": obs.get("textDescription", "Unknown"),
                "description": obs.get("textDescription", ""),
                "icon": obs.get("icon", ""),
                "wind_speed": wind_speed,
            },
            "high": max(forecast_temps) if forecast_temps else None,
            "low": min(forecast_temps) if forecast_temps else None,
            "temp_unit": temp_unit,
            "hourly": hourly,
        }
