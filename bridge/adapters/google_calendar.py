"""Google Calendar adapter — OAuth2 poll-based event fetching."""

from __future__ import annotations

import logging
from datetime import datetime, timedelta, timezone
from typing import Any

import httpx

from .base import AdapterConfig, BaseAdapter

logger = logging.getLogger(__name__)

TOKEN_URI = "https://oauth2.googleapis.com/token"
CALENDAR_API = "https://www.googleapis.com/calendar/v3"


class GoogleCalendarAdapter(BaseAdapter):
    """Polls Google Calendar API for upcoming events.

    Config from BridgeConfig:
      google_calendar.client_id: OAuth2 client ID
      google_calendar.client_secret: OAuth2 client secret
      google_calendar.refresh_token: OAuth2 refresh token (from google_auth.py)
      google_calendar.calendars: list of calendar IDs (default: ["primary"])
      google_calendar.poll_interval: seconds between polls
    """

    def __init__(self, bridge_config: dict[str, Any]) -> None:
        gc_cfg = bridge_config.get("google_calendar", {})
        poll_interval = gc_cfg.get("poll_interval", 300)

        super().__init__(
            name="calendar_google",
            config=AdapterConfig(
                poll_interval=poll_interval,
                ttl=poll_interval * 3,
                max_retries=2,
                backoff_base=10.0,
                backoff_max=120.0,
            ),
        )
        self.client_id = gc_cfg.get("client_id", "")
        self.client_secret = gc_cfg.get("client_secret", "")
        self.refresh_token = gc_cfg.get("refresh_token", "")
        self.calendars: list[str] = gc_cfg.get("calendars", ["primary"])

    async def _get_access_token(self, client: httpx.AsyncClient) -> str:
        """Exchange refresh token for a short-lived access token."""
        resp = await client.post(TOKEN_URI, data={
            "client_id": self.client_id,
            "client_secret": self.client_secret,
            "refresh_token": self.refresh_token,
            "grant_type": "refresh_token",
        })
        resp.raise_for_status()
        return resp.json()["access_token"]

    async def poll(self) -> dict[str, Any]:
        if not self.refresh_token:
            raise ValueError(
                "Google Calendar refresh token is required. "
                "Run google_auth.py to obtain one."
            )

        async with httpx.AsyncClient(timeout=15.0) as client:
            access_token = await self._get_access_token(client)
            headers = {"Authorization": f"Bearer {access_token}"}

            # Time window: now through end of tomorrow
            now = datetime.now(timezone.utc)
            tomorrow_end = (now + timedelta(days=2)).replace(
                hour=0, minute=0, second=0, microsecond=0,
            )

            calendars: dict[str, Any] = {}
            for cal_id in self.calendars:
                try:
                    resp = await client.get(
                        f"{CALENDAR_API}/calendars/{cal_id}/events",
                        headers=headers,
                        params={
                            "timeMin": now.isoformat(),
                            "timeMax": tomorrow_end.isoformat(),
                            "singleEvents": "true",
                            "orderBy": "startTime",
                            "maxResults": 50,
                        },
                    )
                    resp.raise_for_status()
                    data = resp.json()
                    calendars[cal_id] = {
                        "summary": data.get("summary", cal_id),
                        "events": data.get("items", []),
                    }
                except Exception as exc:
                    logger.warning("Google Calendar: failed %s: %s", cal_id, exc)
                    calendars[cal_id] = {"error": str(exc)}

            return {"calendars": calendars}

    def parse(self, raw: dict[str, Any]) -> dict[str, Any]:
        calendars = {}
        total_events = 0

        for cal_id, cal_data in raw.get("calendars", {}).items():
            if "error" in cal_data:
                calendars[cal_id] = {
                    "status": "error",
                    "error": cal_data["error"],
                    "events": [],
                }
                continue

            events = []
            for event in cal_data.get("events", []):
                # Skip cancelled events
                if event.get("status") == "cancelled":
                    continue

                start = event.get("start", {})
                end = event.get("end", {})

                parsed_event = {
                    "id": event.get("id"),
                    "title": event.get("summary", "(No title)"),
                    "start": start.get("dateTime") or start.get("date"),
                    "end": end.get("dateTime") or end.get("date"),
                    "all_day": "date" in start and "dateTime" not in start,
                    "location": event.get("location"),
                    "color_id": event.get("colorId"),
                }
                events.append(parsed_event)

            total_events += len(events)
            calendars[cal_id] = {
                "status": "ok",
                "summary": cal_data.get("summary", cal_id),
                "count": len(events),
                "events": events,
            }

        return {
            "calendars": calendars,
            "total_events": total_events,
        }
