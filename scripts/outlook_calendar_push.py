#!/usr/bin/env python3
"""
outlook_calendar_push.py -- Read Outlook calendar via COM, push to bridge.

Reads upcoming events from the local Outlook desktop client (no OAuth needed)
and either writes JSON locally or POSTs to the DCC bridge service.

Usage:
    python scripts/outlook_calendar_push.py              # Write to stdout
    python scripts/outlook_calendar_push.py --file       # Write to JSON file
    python scripts/outlook_calendar_push.py --push URL   # POST to bridge

Designed to run as a Windows Scheduled Task every 60 seconds.
Requires: pywin32 (pip install pywin32), Outlook running.
"""

import argparse
import json
import sys
from datetime import datetime, timedelta

# Optional: requests for --push mode
try:
    import requests
    HAS_REQUESTS = True
except ImportError:
    HAS_REQUESTS = False

OUTPUT_FILE = "C:/Dev/Desk_Command_Center/.cache/ms_calendar.json"
HOURS_AHEAD = 24
MAX_EVENTS = 50


def _com_time_to_iso(t):
    """Convert COM pywintypes.TimeType or datetime to ISO string."""
    try:
        if hasattr(t, "year"):
            return datetime(
                t.year, t.month, t.day,
                t.hour, t.minute, t.second,
            ).isoformat()
    except Exception:
        pass
    return str(t)


def read_outlook_calendar(hours_ahead=HOURS_AHEAD, max_events=MAX_EVENTS):
    """Read upcoming calendar events from Outlook via COM."""
    import pythoncom
    import win32com.client
    import time

    pythoncom.CoInitialize()

    # Retry loop — Outlook may reject COM calls if busy
    for attempt in range(3):
        try:
            outlook = win32com.client.Dispatch("Outlook.Application")
            ns = outlook.GetNamespace("MAPI")
            break
        except Exception as e:
            if attempt < 2:
                time.sleep(2)
            else:
                raise RuntimeError(f"Cannot connect to Outlook: {e}")

    cal = ns.GetDefaultFolder(9)  # olFolderCalendar

    items = cal.Items
    items.Sort("[Start]")
    items.IncludeRecurrences = True

    now = datetime.now()
    end = now + timedelta(hours=hours_ahead)

    # Outlook DASL date filter format (unambiguous)
    start_str = now.strftime("%m/%d/%Y %I:%M %p")
    end_str = end.strftime("%m/%d/%Y %I:%M %p")
    restriction = f"[Start] >= '{start_str}' AND [Start] <= '{end_str}'"

    filtered = items.Restrict(restriction)

    events = []
    # Use Find/FindNext pattern — faster than enumeration on large calendars
    item = filtered.GetFirst()
    while item is not None and len(events) < max_events:
        try:
            events.append({
                "subject": str(item.Subject or ""),
                "start": _com_time_to_iso(item.Start),
                "end": _com_time_to_iso(item.End),
                "location": str(getattr(item, "Location", "") or ""),
                "organizer": str(getattr(item, "Organizer", "") or ""),
                "is_recurring": bool(getattr(item, "IsRecurring", False)),
                "all_day": bool(getattr(item, "AllDayEvent", False)),
                "busy_status": getattr(item, "BusyStatus", 2),
                "source": "outlook_com",
            })
        except Exception:
            pass
        item = filtered.GetNext()

    return {
        "fetched_at": datetime.now().isoformat(),
        "hours_ahead": hours_ahead,
        "count": len(events),
        "events": events,
    }


def main():
    parser = argparse.ArgumentParser(
        description="Read Outlook calendar via COM and export"
    )
    parser.add_argument(
        "--file", action="store_true",
        help=f"Write JSON to {OUTPUT_FILE}",
    )
    parser.add_argument(
        "--push", type=str, metavar="URL",
        help="POST JSON to bridge endpoint (e.g. http://192.168.50.24:8080/calendar/ms)",
    )
    parser.add_argument(
        "--hours", type=int, default=HOURS_AHEAD,
        help=f"Hours ahead to fetch (default: {HOURS_AHEAD})",
    )
    args = parser.parse_args()

    data = read_outlook_calendar(hours_ahead=args.hours)
    json_str = json.dumps(data, indent=2)

    if args.push:
        if not HAS_REQUESTS:
            print("ERROR: pip install requests", file=sys.stderr)
            sys.exit(1)
        resp = requests.post(args.push, json=data, timeout=10)
        print(f"Pushed {data['count']} events -> {resp.status_code}")
    elif args.file:
        import os
        os.makedirs(os.path.dirname(OUTPUT_FILE), exist_ok=True)
        with open(OUTPUT_FILE, "w") as f:
            f.write(json_str)
        print(f"Wrote {data['count']} events to {OUTPUT_FILE}")
    else:
        print(json_str)


if __name__ == "__main__":
    main()
