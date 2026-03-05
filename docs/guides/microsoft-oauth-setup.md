# Microsoft Azure / Entra ID OAuth2 Setup Guide

> **Purpose:** Configure OAuth2 access to the Microsoft Graph Calendar API so the
> Desk Command Center bridge service (running headless on a Raspberry Pi 5) can
> read Microsoft 365 / Outlook Calendar events.
>
> **Estimated time:** 20--30 minutes
>
> **Prerequisites:**
> - A Microsoft 365 account (personal, work, or school) with calendar data
> - Access to the Azure Portal ([portal.azure.com](https://portal.azure.com))
> - Python 3.9+ on the machine where you will run the verification script
> - `pip install msal requests python-dotenv`

---

## Table of Contents

1. [Register an Application in Entra ID](#1-register-an-application-in-entra-id)
2. [Configure API Permissions](#2-configure-api-permissions)
3. [Set Up Authentication](#3-set-up-authentication)
4. [Create a Client Secret](#4-create-a-client-secret)
5. [Obtain a Refresh Token (Device Code Flow)](#5-obtain-a-refresh-token-device-code-flow)
6. [Save Credentials to .env](#6-save-credentials-to-env)
7. [Verification Test](#7-verification-test)
8. [Captured Values](#8-captured-values)
9. [Security Notes](#9-security-notes)
10. [Troubleshooting](#10-troubleshooting)

---

## 1. Register an Application in Entra ID

An "App Registration" tells Microsoft who your application is and what it is
allowed to do. Every OAuth2 integration starts here.

1. Sign in to the **Azure Portal** at [portal.azure.com](https://portal.azure.com).

2. In the top search bar, type **"App registrations"** and select the
   **App registrations** service from the results.

3. Click **+ New registration** at the top of the page.

4. Fill in the registration form:

   | Field                    | Value                                      |
   |--------------------------|--------------------------------------------|
   | **Name**                 | `Desk Command Center`                      |
   | **Supported account types** | *Accounts in this organizational directory only* (single tenant) -- or choose *Personal Microsoft accounts* if using a personal Outlook.com account. For broadest compatibility, choose **Accounts in any organizational directory and personal Microsoft accounts**. |
   | **Redirect URI**         | Leave blank for now (configured in step 3)  |

5. Click **Register**.

6. You are now on the app's **Overview** page. Record these two values -- you
   will need them later:

   - **Application (client) ID** -- a UUID like `a1b2c3d4-e5f6-...`
   - **Directory (tenant) ID** -- a UUID like `f7g8h9i0-j1k2-...`

   If you selected "personal Microsoft accounts only", your tenant ID value
   will typically be `consumers`. If you selected the multi-tenant + personal
   option, use `common`.

---

## 2. Configure API Permissions

API permissions declare which Microsoft Graph scopes your app may request.
The calendar integration requires two delegated permissions.

1. In your app registration, click **API permissions** in the left sidebar.

2. Click **+ Add a permission**.

3. Select **Microsoft Graph** from the list.

4. Choose **Delegated permissions** (not Application permissions -- we need
   the user's own calendar, not a daemon/service account).

5. Search for and check the following permissions:

   | Permission          | Type      | Description                            |
   |---------------------|-----------|----------------------------------------|
   | `Calendars.Read`    | Delegated | Read the user's calendar events        |
   | `User.Read`         | Delegated | Sign in and read the user's profile    |

   `User.Read` is usually added by default. If it is already listed, you only
   need to add `Calendars.Read`.

6. Click **Add permissions**.

7. **(Optional but recommended for org accounts):** If you have admin rights
   on the tenant, click **Grant admin consent for [your org]**. This
   pre-approves the permissions so end users do not see a consent prompt.
   For personal accounts, consent is granted during the device code flow.

8. Verify the permissions table shows both scopes with a green check under
   "Status" (or "Granted" if admin consent was applied).

---

## 3. Set Up Authentication

The bridge service runs headless on a Raspberry Pi 5, so we use the **device
code flow**. This requires configuring the app as a **public client** with the
appropriate redirect URI.

1. In your app registration, click **Authentication** in the left sidebar.

2. Click **+ Add a platform**.

3. Select **Mobile and desktop applications**.

4. Check the predefined redirect URI:

   ```
   https://login.microsoftonline.com/common/oauth2/nativeclient
   ```

   This is the standard redirect URI for public client / device code flows.

5. Click **Configure**.

6. Scroll down to the **Advanced settings** section on the Authentication page.

7. Set **Allow public client flows** to **Yes**. This is required for the
   device code flow -- without it, the token request will fail with an error
   about confidential clients.

8. Click **Save** at the top of the page.

---

## 4. Create a Client Secret

Even though device code flow is a "public client" flow, having a client secret
allows you to use the authorization code flow as a fallback and provides an
extra layer of identity verification when refreshing tokens.

1. In your app registration, click **Certificates & secrets** in the left sidebar.

2. Click **+ New client secret**.

3. Fill in the form:

   | Field          | Value                          |
   |----------------|--------------------------------|
   | **Description** | `desk-command-center-bridge`  |
   | **Expires**     | 24 months (maximum)           |

4. Click **Add**.

5. **Immediately copy the secret Value** (not the Secret ID). It is shown only
   once. If you navigate away without copying it, you must create a new secret.

   The value looks like: `abC8Q~xYz...`

6. Record the secret securely. You will save it to your `.env` file in step 6.

> **Note:** Client secrets expire. Set a calendar reminder to rotate the secret
> before the expiry date. See [Security Notes](#9-security-notes) for rotation
> guidance.

---

## 5. Obtain a Refresh Token (Device Code Flow)

The device code flow is ideal for headless devices like the Pi 5. You start the
flow on the Pi (or any terminal), then complete sign-in on any device with a
browser (phone, laptop, etc.).

### 5a. Install Dependencies

On the machine where you will run the token acquisition (can be the Pi itself
or any machine -- the resulting refresh token is portable):

```bash
pip install msal requests python-dotenv
```

### 5b. Run the Device Code Flow Script

Create a file called `get_ms_token.py`:

```python
"""
Acquire a Microsoft Graph refresh token using the device code flow.

Usage:
    python get_ms_token.py

You will be prompted to visit a URL and enter a code on any device
with a browser. After sign-in, the refresh token is printed to the
terminal for you to copy into your .env file.
"""

import sys
import msal

# --- Configuration -----------------------------------------------------------
# Replace these with your values from steps 1 and 4.
CLIENT_ID = "YOUR_CLIENT_ID_HERE"
TENANT_ID = "YOUR_TENANT_ID_HERE"         # or "common" / "consumers"
CLIENT_SECRET = "YOUR_CLIENT_SECRET_HERE"  # optional for device code flow

SCOPES = ["Calendars.Read", "User.Read"]
AUTHORITY = f"https://login.microsoftonline.com/{TENANT_ID}"
# -----------------------------------------------------------------------------


def main():
    # Build a confidential or public client depending on whether a secret
    # is provided.
    if CLIENT_SECRET and CLIENT_SECRET != "YOUR_CLIENT_SECRET_HERE":
        app = msal.ConfidentialClientApplication(
            CLIENT_ID,
            authority=AUTHORITY,
            client_credential=CLIENT_SECRET,
        )
    else:
        app = msal.PublicClientApplication(
            CLIENT_ID,
            authority=AUTHORITY,
        )

    # Initiate device code flow
    flow = app.initiate_device_flow(scopes=SCOPES)

    if "user_code" not in flow:
        print("ERROR: Failed to create device flow.")
        print(flow.get("error_description", "Unknown error"))
        sys.exit(1)

    # Display instructions
    print("\n" + "=" * 60)
    print("MICROSOFT SIGN-IN REQUIRED")
    print("=" * 60)
    print(f"\n  1. Open:  {flow['verification_uri']}")
    print(f"  2. Enter: {flow['user_code']}")
    print(f"\n  (Code expires in {flow.get('expires_in', '?')} seconds)")
    print("=" * 60)
    print("\nWaiting for you to complete sign-in...\n")

    # Block until the user completes sign-in (or the code expires)
    result = app.acquire_token_by_device_flow(flow)

    if "access_token" not in result:
        print("ERROR: Token acquisition failed.")
        print(result.get("error_description", "Unknown error"))
        sys.exit(1)

    # Success
    print("Authentication successful!\n")
    print(f"  User:          {result.get('id_token_claims', {}).get('preferred_username', 'N/A')}")
    print(f"  Access Token:  {result['access_token'][:40]}...")
    print(f"  Expires In:    {result.get('expires_in', '?')} seconds")

    # The refresh token is what we need for long-lived headless access
    refresh_token = result.get("refresh_token")
    if refresh_token:
        print(f"\n{'=' * 60}")
        print("REFRESH TOKEN (save this to your .env as MS_REFRESH_TOKEN):")
        print("=" * 60)
        print(f"\n{refresh_token}\n")
    else:
        print("\nWARNING: No refresh token was returned.")
        print("Ensure 'offline_access' is implicitly granted or add it to scopes.")


if __name__ == "__main__":
    main()
```

### 5c. Execute and Authenticate

```bash
python get_ms_token.py
```

1. The script prints a URL and a short code (e.g., `ABCD1234`).
2. On any device with a browser, navigate to `https://microsoft.com/devicelogin`.
3. Enter the code shown in the terminal.
4. Sign in with your Microsoft 365 account.
5. Accept the permissions consent prompt (Calendars.Read, User.Read).
6. Return to the terminal -- the refresh token is displayed.
7. Copy the refresh token; you will need it for the `.env` file.

---

## 6. Save Credentials to .env

On the Raspberry Pi 5 (or wherever the bridge service runs), create or update
the `.env` file in the bridge service directory:

```bash
# Microsoft Graph OAuth2 — Desk Command Center
MS_CLIENT_ID=a1b2c3d4-e5f6-7890-abcd-ef1234567890
MS_CLIENT_SECRET=abC8Q~xYzExampleSecretValueHere
MS_TENANT_ID=f7g8h9i0-j1k2-3456-lmno-pq7890123456
MS_REFRESH_TOKEN=0.AAAA-bBBB...very-long-token-string...
```

Replace the placeholder values with your actual credentials from the previous
steps.

**Critical:** Ensure `.env` is listed in `.gitignore` so secrets are never
committed to version control.

```bash
# Verify .env is git-ignored
echo ".env" >> .gitignore
git check-ignore .env    # should print ".env"
```

---

## 7. Verification Test

This script uses the saved `.env` credentials to acquire a fresh access token
via the refresh token and then queries the Microsoft Graph Calendar API.

Create a file called `verify_ms_calendar.py`:

```python
"""
Verify Microsoft Graph Calendar API access.

Reads credentials from .env, refreshes the access token, and fetches
calendar events for the next 7 days.

Usage:
    pip install msal requests python-dotenv
    python verify_ms_calendar.py
"""

import os
import sys
from datetime import datetime, timedelta, timezone

import msal
import requests
from dotenv import load_dotenv

load_dotenv()

# Read credentials from environment
CLIENT_ID = os.getenv("MS_CLIENT_ID")
CLIENT_SECRET = os.getenv("MS_CLIENT_SECRET")
TENANT_ID = os.getenv("MS_TENANT_ID")
REFRESH_TOKEN = os.getenv("MS_REFRESH_TOKEN")

SCOPES = ["Calendars.Read", "User.Read"]
AUTHORITY = f"https://login.microsoftonline.com/{TENANT_ID}"
GRAPH_ENDPOINT = "https://graph.microsoft.com/v1.0"


def get_access_token():
    """Use the refresh token to obtain a fresh access token."""
    if CLIENT_SECRET:
        app = msal.ConfidentialClientApplication(
            CLIENT_ID,
            authority=AUTHORITY,
            client_credential=CLIENT_SECRET,
        )
    else:
        app = msal.PublicClientApplication(
            CLIENT_ID,
            authority=AUTHORITY,
        )

    result = app.acquire_token_by_refresh_token(REFRESH_TOKEN, scopes=SCOPES)

    if "access_token" not in result:
        print("ERROR: Failed to refresh access token.")
        print(result.get("error_description", "Unknown error"))
        sys.exit(1)

    # If a new refresh token was returned, print a notice
    new_rt = result.get("refresh_token")
    if new_rt and new_rt != REFRESH_TOKEN:
        print("NOTE: A new refresh token was issued.")
        print("Update MS_REFRESH_TOKEN in your .env with this value:")
        print(f"  {new_rt}\n")

    return result["access_token"]


def fetch_calendar_events(access_token):
    """Fetch calendar events for the next 7 days."""
    now = datetime.now(timezone.utc)
    end = now + timedelta(days=7)

    params = {
        "startDateTime": now.strftime("%Y-%m-%dT%H:%M:%S.0000000"),
        "endDateTime": end.strftime("%Y-%m-%dT%H:%M:%S.0000000"),
        "$orderby": "start/dateTime",
        "$top": "20",
        "$select": "subject,start,end,location,isAllDay",
    }

    headers = {
        "Authorization": f"Bearer {access_token}",
        "Prefer": 'outlook.timezone="UTC"',
    }

    response = requests.get(
        f"{GRAPH_ENDPOINT}/me/calendarview",
        headers=headers,
        params=params,
        timeout=15,
    )

    if response.status_code != 200:
        print(f"ERROR: Graph API returned {response.status_code}")
        print(response.text)
        sys.exit(1)

    return response.json().get("value", [])


def main():
    print("Desk Command Center - Microsoft Calendar Verification")
    print("=" * 55)

    # Validate environment
    missing = []
    for var in ("MS_CLIENT_ID", "MS_TENANT_ID", "MS_REFRESH_TOKEN"):
        if not os.getenv(var):
            missing.append(var)
    if missing:
        print(f"ERROR: Missing environment variables: {', '.join(missing)}")
        print("Ensure your .env file is populated. See step 6 of the setup guide.")
        sys.exit(1)

    # Get access token
    print("\n[1/2] Refreshing access token...")
    token = get_access_token()
    print("  OK - Access token acquired.\n")

    # Fetch events
    print("[2/2] Fetching calendar events (next 7 days)...")
    events = fetch_calendar_events(token)

    if not events:
        print("  No upcoming events found in the next 7 days.")
        print("  (This is OK if your calendar is empty.)\n")
    else:
        print(f"  Found {len(events)} event(s):\n")
        for i, event in enumerate(events, 1):
            subject = event.get("subject", "(no subject)")
            start = event.get("start", {}).get("dateTime", "?")[:16]
            end_time = event.get("end", {}).get("dateTime", "?")[:16]
            location = event.get("location", {}).get("displayName", "")
            all_day = event.get("isAllDay", False)

            time_str = "All day" if all_day else f"{start} - {end_time}"
            loc_str = f"  @ {location}" if location else ""

            print(f"  {i:2d}. {subject}")
            print(f"      {time_str}{loc_str}")
            print()

    print("=" * 55)
    print("Verification PASSED. Calendar API access is working.")
    print("=" * 55)


if __name__ == "__main__":
    main()
```

Run the verification:

```bash
python verify_ms_calendar.py
```

Expected output:

```
Desk Command Center - Microsoft Calendar Verification
=======================================================

[1/2] Refreshing access token...
  OK - Access token acquired.

[2/2] Fetching calendar events (next 7 days)...
  Found 3 event(s):

   1. Weekly Team Standup
      2026-03-06T09:00 - 2026-03-06T09:30  @ Conference Room B

   2. Project Review
      2026-03-07T14:00 - 2026-03-07T15:00

   3. Lunch with Client
      2026-03-09T12:00 - 2026-03-09T13:00  @ Downtown Cafe

=======================================================
Verification PASSED. Calendar API access is working.
=======================================================
```

---

## 8. Captured Values

Use this table to record your configuration values. **Do not fill in the actual
secret or refresh token here** -- keep those only in your `.env` file.

| Variable           | Value                                       | Where to find it                    |
|--------------------|---------------------------------------------|-------------------------------------|
| `MS_CLIENT_ID`     | `________________________________`          | App registration > Overview         |
| `MS_TENANT_ID`     | `________________________________`          | App registration > Overview         |
| `MS_CLIENT_SECRET` | *(stored in .env only)*                     | Certificates & secrets              |
| `MS_REFRESH_TOKEN` | *(stored in .env only)*                     | Output of `get_ms_token.py`         |
| Secret Expiry Date | `________________________________`          | Certificates & secrets > Expires    |
| Account Type       | `[ ] Work/School  [ ] Personal`             | Depends on your M365 subscription   |

---

## 9. Security Notes

### Token Storage

- **Never commit** `.env`, client secrets, or refresh tokens to git. Verify
  with `git check-ignore .env` before every commit.
- On the Pi, restrict file permissions: `chmod 600 .env`
- Consider using a secrets manager (e.g., HashiCorp Vault, 1Password CLI)
  for production deployments.

### Secret Rotation

Client secrets have a maximum lifetime of 24 months. Plan for rotation:

1. Before the current secret expires, create a **new** client secret in Azure
   Portal (Certificates & secrets > + New client secret).
2. Update `MS_CLIENT_SECRET` in your `.env` with the new value.
3. Restart the bridge service.
4. Verify with `python verify_ms_calendar.py`.
5. Delete the **old** secret from Azure Portal.

Set a calendar reminder 30 days before expiry.

### Refresh Token Lifecycle

- Refresh tokens for Microsoft Graph are typically valid for 90 days of
  inactivity. Each use extends the window.
- If the bridge service runs at least once within any 90-day window, the
  refresh token remains valid indefinitely (it is "rolling").
- If the refresh token expires, re-run `get_ms_token.py` to obtain a new one
  via the device code flow.
- Some tenant policies (Conditional Access) can revoke refresh tokens sooner.
  Check with your IT administrator if you encounter unexpected token failures.

### Principle of Least Privilege

- The app only requests `Calendars.Read` (read-only) and `User.Read`.
- It does **not** request `Calendars.ReadWrite`, `Mail.Read`, or any other
  scope. The bridge service has no ability to modify calendar events or
  read email.
- If you need write access later, add the scope in API permissions and
  re-run the device code flow to obtain a new consent + refresh token.

### Network Security

- All communication with Microsoft Graph uses HTTPS/TLS.
- The bridge service never exposes OAuth tokens over the local network.
- Access tokens are short-lived (typically 60--90 minutes) and are only held
  in memory during API calls.

---

## 10. Troubleshooting

### "AADSTS7000218: The request body must contain the following parameter: client_assertion or client_secret"

You are using a confidential client flow but did not provide a client secret.
Either add `CLIENT_SECRET` to the script or ensure **Allow public client flows**
is set to **Yes** in Authentication settings (step 3.7).

### "AADSTS50076: Due to a configuration change made by your administrator..."

Your tenant requires multi-factor authentication for token grants. Complete MFA
during the device code flow sign-in step.

### "AADSTS65001: The user or administrator has not consented to use the application"

The required permissions have not been consented. Either:
- Re-run the device code flow and accept the consent prompt, or
- Ask a tenant admin to grant admin consent in API permissions.

### "No refresh token was returned"

This can happen if:
- The `offline_access` scope is blocked by tenant policy.
- You used a flow that does not issue refresh tokens (e.g., client credentials).
Ensure you are using the device code flow with delegated permissions.

### "Access token expired" errors from the bridge service

The bridge service must refresh the access token before each API call (or cache
it for up to 60 minutes). Verify that the token refresh logic in the bridge
correctly uses `acquire_token_by_refresh_token()`.

### Empty calendar results

- Confirm events exist in the queried time range.
- Check the `Prefer: outlook.timezone` header matches your expectations.
- Verify the authenticated account is the one with the calendar events (check
  `preferred_username` in the token claims).
