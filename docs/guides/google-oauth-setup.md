# Google Cloud Console — OAuth2 Setup for Calendar API

> **Context:** The Desk Command Center bridge service (running on Raspberry Pi 5)
> needs read-only access to Google Calendar. This guide walks through the complete
> Google Cloud Console setup to obtain OAuth2 credentials and a long-lived refresh
> token.
>
> **Target scope:** `https://www.googleapis.com/auth/calendar.readonly`
> **Credential type:** Desktop application (no redirect URI required)

---

## Table of Contents

1. [Create a Google Cloud Project](#1-create-a-google-cloud-project)
2. [Enable the Google Calendar API](#2-enable-the-google-calendar-api)
3. [Configure the OAuth Consent Screen](#3-configure-the-oauth-consent-screen)
4. [Create OAuth 2.0 Credentials](#4-create-oauth-20-credentials)
5. [Obtain a Refresh Token](#5-obtain-a-refresh-token)
6. [Save Credentials to .env](#6-save-credentials-to-env)
7. [Verification Test](#7-verification-test)
8. [Captured Values](#8-captured-values)
9. [Security Notes](#9-security-notes)
10. [Troubleshooting](#10-troubleshooting)

---

## 1. Create a Google Cloud Project

1. Open your browser and navigate to the Google Cloud Console:
   `https://console.cloud.google.com/`

2. Sign in with the Google account whose calendars you want to access.

3. In the top navigation bar, click the **project selector** dropdown (to the
   right of "Google Cloud"). It displays your current project name or
   "Select a project".

4. In the modal that appears, click **New Project** (upper-right corner).

5. Fill in the project details:
   - **Project name:** `desk-command-center` (or any descriptive name)
   - **Organization:** Leave as "No organization" for personal accounts, or
     select your org if using a Workspace account.
   - **Location:** Leave as default.

6. Click **Create**. Wait a few seconds for the project to provision.

7. Once created, select the new project from the project selector dropdown to
   make it active. Confirm the project name appears in the top bar.

8. Note your **Project ID** — visible on the project dashboard or in the URL
   (e.g., `desk-command-center-123456`). Record it in the
   [Captured Values](#8-captured-values) section below.

---

## 2. Enable the Google Calendar API

1. In the left sidebar, navigate to **APIs & Services > Library**.
   Alternatively, use the search bar at the top and type "API Library".

2. In the API Library search box, type **Google Calendar API**.

3. Click on **Google Calendar API** in the results (published by Google).

4. On the API detail page, click the blue **Enable** button.

5. Wait for the API to be enabled. You will be redirected to the API overview
   page. Confirm that the status shows "API enabled" (or the **Manage** button
   is now visible instead of **Enable**).

---

## 3. Configure the OAuth Consent Screen

Before creating credentials, you must configure the consent screen that users
see when authorizing your app.

1. In the left sidebar, navigate to **APIs & Services > OAuth consent screen**.

2. Select **User Type:**
   - Choose **External** (available to any Google account).
   - If you are on a Google Workspace and want to restrict to your org, choose
     **Internal** instead.
   - Click **Create**.

3. Fill in the **App information** form:
   - **App name:** `Desk Command Center`
   - **User support email:** Select your email from the dropdown.
   - **App logo:** Optional — skip for now.

4. Scroll down to **App domain** — leave all fields blank (not required for
   Desktop apps in testing mode).

5. Under **Developer contact information**, enter your email address.

6. Click **Save and Continue**.

### Scopes Configuration

7. On the **Scopes** page, click **Add or Remove Scopes**.

8. In the search/filter box, type `calendar.readonly`.

9. Check the box next to:
   ```
   https://www.googleapis.com/auth/calendar.readonly
   ```
   Description: "See, edit, share, and permanently delete all the calendars
   you can access using Google Calendar" — despite the description, the
   `.readonly` scope only grants read access.

10. Click **Update** at the bottom of the scope selection panel.

11. Verify the scope appears in the "Your sensitive scopes" table.

12. Click **Save and Continue**.

### Test Users

13. On the **Test users** page, click **Add Users**.

14. Enter the email address of the Google account whose calendar you want to
    read. This must be a real Google account email.

15. Click **Add**, then **Save and Continue**.

16. Review the summary and click **Back to Dashboard**.

> **Important:** While the app is in "Testing" mode, only the test users you
> added can authorize the app. This is fine for a personal device like the
> Desk Command Center — you do not need to go through Google's verification
> process. The app can remain in testing mode indefinitely for personal use.

> **Token expiry note:** Apps in "Testing" mode issue refresh tokens that
> expire after 7 days. To get non-expiring refresh tokens, you must either:
> (a) publish the app (which may require verification), or (b) move the app
> to "In production" status. See the [Troubleshooting](#10-troubleshooting)
> section for details on moving to production without verification.

---

## 4. Create OAuth 2.0 Credentials

1. In the left sidebar, navigate to **APIs & Services > Credentials**.

2. At the top, click **+ Create Credentials** and select **OAuth client ID**.

3. For **Application type**, select **Desktop app**.

   > We use Desktop app (not Web application) because the bridge service runs
   > on a Raspberry Pi 5 headlessly — there is no browser-based redirect flow
   > in production. The initial token exchange is done once during setup.

4. **Name:** `desk-command-center-pi` (or any descriptive name).

5. Click **Create**.

6. A dialog appears showing your credentials:
   - **Client ID:** A long string ending in `.apps.googleusercontent.com`
   - **Client secret:** A shorter alphanumeric string prefixed with `GOCSPX-`

7. Click **Download JSON** to save the credentials file. Store it securely.

8. Record both values in the [Captured Values](#8-captured-values) section.

9. Click **OK** to close the dialog.

---

## 5. Obtain a Refresh Token

The bridge service needs a **refresh token** to obtain access tokens without
user interaction. You perform this step once during setup.

### Option A: Python Script (Recommended)

This method runs a local OAuth flow that opens your browser, lets you authorize,
and captures the refresh token.

#### Prerequisites

```bash
pip install google-auth-oauthlib google-api-python-client
```

#### Script: `get_refresh_token.py`

Create a temporary script (do NOT commit this to the repository):

```python
#!/usr/bin/env python3
"""
One-time script to obtain a Google OAuth2 refresh token.
Run this on a machine with a browser (your workstation, not the Pi).
"""

import json
from google_auth_oauthlib.flow import InstalledAppFlow

SCOPES = ["https://www.googleapis.com/auth/calendar.readonly"]

# Replace these with your actual values from Step 4
CLIENT_CONFIG = {
    "installed": {
        "client_id": "YOUR_CLIENT_ID.apps.googleusercontent.com",
        "client_secret": "YOUR_CLIENT_SECRET",
        "auth_uri": "https://accounts.google.com/o/oauth2/auth",
        "token_uri": "https://oauth2.googleapis.com/token",
        "redirect_uris": ["http://localhost"],
    }
}


def main():
    flow = InstalledAppFlow.from_client_config(CLIENT_CONFIG, SCOPES)

    # This opens a browser window for authorization.
    # Use run_console() instead if you are on a headless machine.
    credentials = flow.run_local_server(port=8080)

    print("\n=== OAuth2 Credentials ===")
    print(f"Access Token:  {credentials.token}")
    print(f"Refresh Token: {credentials.refresh_token}")
    print(f"Token URI:     {credentials.token_uri}")
    print(f"Client ID:     {credentials.client_id}")
    print(f"Client Secret: {credentials.client_secret}")
    print("\nSave the Refresh Token — you will need it for .env configuration.")
    print("This script can be deleted after you have recorded the token.\n")


if __name__ == "__main__":
    main()
```

#### Running the Script

```bash
python get_refresh_token.py
```

1. Your default browser opens to a Google sign-in page.

2. Sign in with the Google account you added as a test user in Step 3.

3. You will see a warning: "Google hasn't verified this app." This is expected
   for apps in testing mode. Click **Continue**.

4. Review the permissions requested (read-only calendar access) and click
   **Continue**.

5. The browser shows "The authentication flow has completed." You can close
   the browser tab.

6. Back in the terminal, the script prints your credentials. Copy the
   **Refresh Token** value.

7. Record the refresh token in the [Captured Values](#8-captured-values)
   section.

8. **Delete the script** after you have recorded the token. Do not commit it
   to version control.

### Option B: OAuth 2.0 Playground (Manual)

Use this if you cannot run Python locally.

1. Open: `https://developers.google.com/oauthplayground/`

2. Click the **gear icon** (Settings) in the upper-right corner.

3. Check **Use your own OAuth credentials**.

4. Enter your **Client ID** and **Client Secret** from Step 4.

5. Close the settings panel.

6. In the left panel ("Step 1 — Select & authorize APIs"), scroll down to
   **Google Calendar API v3** and select:
   ```
   https://www.googleapis.com/auth/calendar.readonly
   ```

7. Click **Authorize APIs**.

8. Sign in with your test user account and grant permission (same flow as
   Option A — accept the "unverified app" warning).

9. On the "Step 2" screen, click **Exchange authorization code for tokens**.

10. The right panel shows the response JSON. Copy the `refresh_token` value.

11. Record it in the [Captured Values](#8-captured-values) section.

> **Note on the Playground:** The OAuth Playground at
> `developers.google.com/oauthplayground` is a Google-hosted tool. When you
> use your own credentials (step 2-4 above), tokens are issued to your app,
> not to Google's playground app. The refresh token is valid for your
> client ID/secret pair.

---

## 6. Save Credentials to .env

On the Raspberry Pi 5 (or wherever the bridge service runs), create or update
the `.env` file with the three credential values:

```bash
# Google Calendar API — OAuth2 Credentials
# Obtained via docs/guides/google-oauth-setup.md
GOOGLE_CLIENT_ID=your-client-id.apps.googleusercontent.com
GOOGLE_CLIENT_SECRET=GOCSPX-your-client-secret
GOOGLE_REFRESH_TOKEN=1//your-refresh-token
```

### File permissions (Linux/Pi)

Restrict the `.env` file so only the service user can read it:

```bash
chmod 600 .env
chown pi:pi .env   # adjust username as needed
```

### Gitignore verification

Confirm that `.env` is listed in `.gitignore` so credentials are never committed:

```bash
grep -q '\.env' .gitignore && echo "OK: .env is gitignored" || echo "WARNING: add .env to .gitignore"
```

---

## 7. Verification Test

Run this script to confirm that your credentials work and calendar events can
be fetched.

### Prerequisites

```bash
pip install google-auth google-auth-httplib2 google-api-python-client python-dotenv
```

### Script: `verify_calendar.py`

```python
#!/usr/bin/env python3
"""
Verify Google Calendar API access using OAuth2 refresh token.
Reads credentials from .env and lists the next 10 upcoming events.
"""

import os
import sys
from datetime import datetime, timezone

from dotenv import load_dotenv
from google.auth.transport.requests import Request
from google.oauth2.credentials import Credentials
from googleapiclient.discovery import build
from googleapiclient.errors import HttpError

# Load credentials from .env
load_dotenv()

CLIENT_ID = os.getenv("GOOGLE_CLIENT_ID")
CLIENT_SECRET = os.getenv("GOOGLE_CLIENT_SECRET")
REFRESH_TOKEN = os.getenv("GOOGLE_REFRESH_TOKEN")

SCOPES = ["https://www.googleapis.com/auth/calendar.readonly"]
TOKEN_URI = "https://oauth2.googleapis.com/token"


def main():
    # Validate that all credentials are present
    missing = []
    if not CLIENT_ID:
        missing.append("GOOGLE_CLIENT_ID")
    if not CLIENT_SECRET:
        missing.append("GOOGLE_CLIENT_SECRET")
    if not REFRESH_TOKEN:
        missing.append("GOOGLE_REFRESH_TOKEN")

    if missing:
        print(f"ERROR: Missing environment variables: {', '.join(missing)}")
        print("Ensure .env is populated per docs/guides/google-oauth-setup.md")
        sys.exit(1)

    # Build credentials from refresh token
    credentials = Credentials(
        token=None,
        refresh_token=REFRESH_TOKEN,
        client_id=CLIENT_ID,
        client_secret=CLIENT_SECRET,
        token_uri=TOKEN_URI,
        scopes=SCOPES,
    )

    # Force a token refresh to validate credentials
    try:
        credentials.refresh(Request())
        print("OK: Token refresh successful.")
    except Exception as e:
        print(f"ERROR: Token refresh failed: {e}")
        print("\nPossible causes:")
        print("  - Client ID or secret is incorrect")
        print("  - Refresh token has expired (testing mode tokens expire in 7 days)")
        print("  - The Google Calendar API is not enabled for this project")
        sys.exit(1)

    # Fetch upcoming events
    try:
        service = build("calendar", "v3", credentials=credentials)
        now = datetime.now(timezone.utc).isoformat()

        print(f"\nFetching next 10 events from primary calendar...\n")
        result = service.events().list(
            calendarId="primary",
            timeMin=now,
            maxResults=10,
            singleEvents=True,
            orderBy="startTime",
        ).execute()

        events = result.get("items", [])

        if not events:
            print("No upcoming events found (calendar may be empty).")
            print("This is still a successful API connection.")
        else:
            for event in events:
                start = event["start"].get("dateTime", event["start"].get("date"))
                summary = event.get("summary", "(No title)")
                print(f"  {start}  {summary}")

        print("\n--- Verification complete. Calendar API access is working. ---")

    except HttpError as e:
        print(f"ERROR: Calendar API request failed: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
```

### Running the Verification

```bash
python verify_calendar.py
```

**Expected output (success):**

```
OK: Token refresh successful.

Fetching next 10 events from primary calendar...

  2026-03-06T09:00:00-05:00  Team standup
  2026-03-06T14:00:00-05:00  Project review
  ...

--- Verification complete. Calendar API access is working. ---
```

**If you see errors**, check the [Troubleshooting](#10-troubleshooting) section.

---

## 8. Captured Values

Record your project-specific values here during setup. **Never commit actual
secrets to version control** — this section is a template for your local notes.

| Value               | Where to Find                           | Your Value               |
|---------------------|-----------------------------------------|--------------------------|
| GCP Project ID      | Cloud Console dashboard, URL bar        | `___________________`    |
| GCP Project Number  | Cloud Console > Project Settings        | `___________________`    |
| OAuth Client ID     | APIs & Services > Credentials           | `___________________`    |
| OAuth Client Secret | APIs & Services > Credentials           | `___________________`    |
| Refresh Token       | Output of get_refresh_token.py          | `___________________`    |
| Test User Email     | OAuth consent screen > Test users       | `___________________`    |
| Calendar ID         | Usually `primary` or your email address | `___________________`    |

> **Tip:** Store these values in a password manager (1Password, Bitwarden, etc.)
> rather than in plain text files.

---

## 9. Security Notes

### Token Storage

- **Never commit credentials to Git.** The `.env` file must be in `.gitignore`.
- **Restrict file permissions** on the Pi: `chmod 600 .env`
- **Refresh tokens are long-lived.** If compromised, an attacker gains read
  access to your calendar. Treat them like passwords.

### Token Revocation

If you suspect a token has been compromised:

1. Go to `https://myaccount.google.com/permissions`
2. Find "Desk Command Center" in the list of apps with access.
3. Click **Remove Access**. This immediately invalidates all tokens.
4. Re-run the token acquisition flow (Step 5) to get new credentials.

Alternatively, revoke programmatically:

```bash
curl -X POST "https://oauth2.googleapis.com/revoke?token=YOUR_REFRESH_TOKEN"
```

### Scope Minimization

The `calendar.readonly` scope grants the minimum access needed. It allows
reading events but cannot create, modify, or delete them. Do not request
broader scopes unless the feature set requires it.

### Testing Mode vs. Production

- **Testing mode:** Refresh tokens expire after **7 days**. You must
  re-authorize periodically. Suitable for initial development.
- **Production mode:** Refresh tokens do not expire (unless revoked). To
  switch, see [Troubleshooting](#10-troubleshooting) below.

### Credential Rotation

Periodically rotate your client secret:

1. Go to **APIs & Services > Credentials**.
2. Click on your OAuth client ID.
3. Click **Reset Secret** (note: this invalidates the old secret immediately).
4. Update `.env` with the new secret.
5. Re-acquire a refresh token (the old one is invalidated by the secret reset).

---

## 10. Troubleshooting

### "Token has been expired or revoked"

**Cause:** The refresh token expired (7-day limit in testing mode) or was
manually revoked.

**Fix:** Re-run the token acquisition script from Step 5 to get a new refresh
token.

**Permanent fix:** Move the app out of testing mode:

1. Go to **APIs & Services > OAuth consent screen**.
2. Click **Publish App**.
3. Since the app uses only non-sensitive or sensitive scopes (not restricted
   scopes), Google may not require full verification.
4. If prompted for verification, you can submit a brief form. For personal-use
   apps with fewer than 100 users, verification is typically straightforward.
5. After publishing, newly issued refresh tokens will not expire.

### "Access Not Configured" or 403 Error

**Cause:** The Google Calendar API is not enabled for your project.

**Fix:** Go to **APIs & Services > Library**, search for "Google Calendar API",
and click **Enable**.

### "redirect_uri_mismatch"

**Cause:** The credential type is set to "Web application" instead of "Desktop
app", or the redirect URI does not match.

**Fix:** Delete the credential and create a new one with application type
**Desktop app** (Step 4).

### "This app is blocked" (Error 403: org_policy)

**Cause:** Your Google Workspace admin has restricted third-party app access.

**Fix:** Contact your Workspace admin to allow the app, or use a personal
Google account instead.

### Empty Event List

**Not an error.** If your calendar has no upcoming events, the API returns an
empty list. The verification script confirms this is a successful connection.

### Network Errors on the Pi

**Cause:** DNS resolution or firewall issues on the Raspberry Pi.

**Fix:**
```bash
# Test DNS resolution
nslookup oauth2.googleapis.com

# Test HTTPS connectivity
curl -I https://www.googleapis.com/calendar/v3/calendars/primary

# Check system time (OAuth requires accurate clock)
date
# If time is wrong:
sudo timedatectl set-ntp true
```

---

## Quick Reference

### Relevant URLs

| Resource                  | URL                                                              |
|---------------------------|------------------------------------------------------------------|
| Google Cloud Console      | `https://console.cloud.google.com/`                              |
| API Library               | `https://console.cloud.google.com/apis/library`                  |
| Credentials               | `https://console.cloud.google.com/apis/credentials`              |
| OAuth Consent Screen      | `https://console.cloud.google.com/apis/credentials/consent`      |
| OAuth Playground           | `https://developers.google.com/oauthplayground/`                |
| Revoke App Access         | `https://myaccount.google.com/permissions`                       |
| Calendar API Docs         | `https://developers.google.com/calendar/api/v3/reference`        |

### Environment Variables Summary

| Variable                | Description                          | Example Format                           |
|-------------------------|--------------------------------------|------------------------------------------|
| `GOOGLE_CLIENT_ID`      | OAuth client identifier              | `123456-abc.apps.googleusercontent.com`  |
| `GOOGLE_CLIENT_SECRET`  | OAuth client secret                  | `GOCSPX-xxxxxxxxxxxxxxxx`               |
| `GOOGLE_REFRESH_TOKEN`  | Long-lived token for offline access  | `1//0xxxxxxxxxxxxxxxxxxxxxxx`            |
