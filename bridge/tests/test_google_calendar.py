"""Tests for Google Calendar adapter — parsing and poll-once with mocked data."""

from unittest.mock import AsyncMock

import pytest

from adapters import AdapterScheduler, AdapterStatus, TTLCache
from adapters.google_calendar import GoogleCalendarAdapter


GC_CONFIG = {
    "google_calendar": {
        "client_id": "test-client-id",
        "client_secret": "test-client-secret",
        "refresh_token": "test-refresh-token",
        "calendars": ["primary", "work@example.com"],
        "poll_interval": 300,
    },
}

SAMPLE_RAW = {
    "calendars": {
        "primary": {
            "summary": "Personal",
            "events": [
                {
                    "id": "evt1",
                    "summary": "Team standup",
                    "start": {"dateTime": "2025-11-14T09:00:00-05:00"},
                    "end": {"dateTime": "2025-11-14T09:30:00-05:00"},
                    "location": "Conference Room A",
                    "status": "confirmed",
                },
                {
                    "id": "evt2",
                    "summary": "Dentist appointment",
                    "start": {"date": "2025-11-15"},
                    "end": {"date": "2025-11-16"},
                    "status": "confirmed",
                },
                {
                    "id": "evt3",
                    "summary": "Cancelled meeting",
                    "start": {"dateTime": "2025-11-14T14:00:00-05:00"},
                    "end": {"dateTime": "2025-11-14T15:00:00-05:00"},
                    "status": "cancelled",
                },
            ],
        },
        "work@example.com": {
            "error": "403 Forbidden",
        },
    },
}


class TestGoogleCalendarAdapter:
    def test_init(self):
        adapter = GoogleCalendarAdapter(GC_CONFIG)
        assert adapter.name == "calendar_google"
        assert adapter.client_id == "test-client-id"
        assert adapter.refresh_token == "test-refresh-token"
        assert adapter.calendars == ["primary", "work@example.com"]

    def test_init_defaults(self):
        adapter = GoogleCalendarAdapter({})
        assert adapter.calendars == ["primary"]
        assert adapter.refresh_token == ""

    def test_parse_events(self):
        adapter = GoogleCalendarAdapter(GC_CONFIG)
        parsed = adapter.parse(SAMPLE_RAW)

        primary = parsed["calendars"]["primary"]
        assert primary["status"] == "ok"
        assert primary["summary"] == "Personal"
        # Cancelled event should be filtered out
        assert primary["count"] == 2
        assert parsed["total_events"] == 2

    def test_parse_event_fields(self):
        adapter = GoogleCalendarAdapter(GC_CONFIG)
        parsed = adapter.parse(SAMPLE_RAW)

        events = parsed["calendars"]["primary"]["events"]
        standup = events[0]
        assert standup["title"] == "Team standup"
        assert standup["start"] == "2025-11-14T09:00:00-05:00"
        assert standup["location"] == "Conference Room A"
        assert standup["all_day"] is False

    def test_parse_all_day_event(self):
        adapter = GoogleCalendarAdapter(GC_CONFIG)
        parsed = adapter.parse(SAMPLE_RAW)

        events = parsed["calendars"]["primary"]["events"]
        dentist = events[1]
        assert dentist["title"] == "Dentist appointment"
        assert dentist["start"] == "2025-11-15"
        assert dentist["all_day"] is True

    def test_parse_error_calendar(self):
        adapter = GoogleCalendarAdapter(GC_CONFIG)
        parsed = adapter.parse(SAMPLE_RAW)

        work = parsed["calendars"]["work@example.com"]
        assert work["status"] == "error"
        assert "403" in work["error"]

    def test_parse_empty(self):
        adapter = GoogleCalendarAdapter(GC_CONFIG)
        parsed = adapter.parse({"calendars": {}})
        assert parsed["total_events"] == 0
        assert parsed["calendars"] == {}

    @pytest.mark.asyncio
    async def test_poll_requires_refresh_token(self):
        adapter = GoogleCalendarAdapter({})
        with pytest.raises(ValueError, match="refresh token"):
            await adapter.poll()

    @pytest.mark.asyncio
    async def test_poll_once_with_mock(self):
        adapter = GoogleCalendarAdapter(GC_CONFIG)
        cache = TTLCache()
        sched = AdapterScheduler(cache=cache)
        sched.register(adapter)

        adapter.poll = AsyncMock(return_value=SAMPLE_RAW)

        result = await sched.poll_once(adapter)
        assert result is not None
        assert "primary" in result["calendars"]
        assert result["total_events"] == 2
        assert adapter.state.status == AdapterStatus.OK
        assert cache.get("calendar_google") is not None
