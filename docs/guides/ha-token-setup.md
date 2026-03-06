# Home Assistant — Long-Lived Access Token Setup

> **For:** Desk Command Center bridge service
> **Time:** ~2 minutes

---

## Prerequisites

- Home Assistant running and accessible on your network
- Admin account with login credentials

---

## Step 1: Log Into Home Assistant

1. Open your Home Assistant instance in a browser (e.g., `http://your-ha-host:8123`)
2. Log in with your admin credentials

## Step 2: Navigate to Profile

1. Click your user avatar (bottom-left of the sidebar)
2. Or go directly to: `http://your-ha-host:8123/profile`

## Step 3: Create Long-Lived Access Token

1. Scroll down to the **Long-Lived Access Tokens** section
2. Click **Create Token**
3. Name it: `Desk Command Center`
4. Click **OK**
5. **IMPORTANT:** Copy the token immediately — it will NOT be shown again

## Step 4: Save Token to .env

Add to your `.env` file (NOT `.env.example`):

```
HA_LONG_LIVED_TOKEN=<paste-your-token-here>
```

## Step 5: Verify Token

Run this curl command to verify the token works:

```bash
curl -s -H "Authorization: Bearer YOUR_TOKEN_HERE" \
  http://your-ha-host:8123/api/ | python -m json.tool
```

Expected response:
```json
{
    "message": "API running."
}
```

### Python verification:

```python
import requests

HA_URL = "http://your-ha-host:8123"
TOKEN = "YOUR_TOKEN_HERE"

response = requests.get(
    f"{HA_URL}/api/states",
    headers={"Authorization": f"Bearer {TOKEN}"}
)
print(f"Status: {response.status_code}")
print(f"Entities found: {len(response.json())}")
```

---

## Token Details

| Property | Value |
|----------|-------|
| Type | Long-Lived Access Token |
| Expiry | 10 years (default) |
| Scope | Full API access for the creating user |
| Revoke | Profile → Long-Lived Access Tokens → Delete |

## Security Notes

- Store ONLY in `.env` (gitignored)
- Never commit tokens to the repository
- If compromised: revoke immediately via Profile → Delete token
- Create a dedicated HA user for the bridge service (recommended for production)
