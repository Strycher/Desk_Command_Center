"""One-time Google Calendar OAuth2 authorization helper.

Run this script on a machine with a browser to obtain a refresh token.
The refresh token is then stored in bridge_config.json for the adapter.

Usage:
    python google_auth.py <path_to_client_secret.json>
"""

from __future__ import annotations

import json
import sys
import urllib.parse
import webbrowser
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

SCOPES = "https://www.googleapis.com/auth/calendar.readonly"
REDIRECT_URI = "http://localhost:8085"


def main() -> None:
    if len(sys.argv) < 2:
        print("Usage: python google_auth.py <path_to_client_secret.json>")
        sys.exit(1)

    secret_path = Path(sys.argv[1])
    if not secret_path.exists():
        print(f"File not found: {secret_path}")
        sys.exit(1)

    creds = json.loads(secret_path.read_text())
    installed = creds.get("installed", {})
    client_id = installed["client_id"]
    client_secret = installed["client_secret"]
    token_uri = installed["token_uri"]

    # Build authorization URL
    auth_params = urllib.parse.urlencode({
        "client_id": client_id,
        "redirect_uri": REDIRECT_URI,
        "response_type": "code",
        "scope": SCOPES,
        "access_type": "offline",
        "prompt": "consent",
    })
    auth_url = f"{installed['auth_uri']}?{auth_params}"

    print("\nOpening browser for Google authorization...")
    print(f"If the browser doesn't open, visit:\n{auth_url}\n")
    webbrowser.open(auth_url)

    # Start local server to catch the redirect
    auth_code = None

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            nonlocal auth_code
            query = urllib.parse.urlparse(self.path).query
            params = urllib.parse.parse_qs(query)
            auth_code = params.get("code", [None])[0]

            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            if auth_code:
                self.wfile.write(
                    b"<h2>Authorization successful!</h2>"
                    b"<p>You can close this tab and return to the terminal.</p>"
                )
            else:
                error = params.get("error", ["unknown"])[0]
                self.wfile.write(
                    f"<h2>Authorization failed: {error}</h2>".encode()
                )

        def log_message(self, format, *args):
            pass  # Suppress server logs

    server = HTTPServer(("localhost", 8085), Handler)
    print("Waiting for authorization callback on http://localhost:8085 ...")
    server.handle_request()  # Handle exactly one request
    server.server_close()

    if not auth_code:
        print("Authorization failed — no code received.")
        sys.exit(1)

    # Exchange code for tokens
    import httpx

    resp = httpx.post(token_uri, data={
        "code": auth_code,
        "client_id": client_id,
        "client_secret": client_secret,
        "redirect_uri": REDIRECT_URI,
        "grant_type": "authorization_code",
    })
    resp.raise_for_status()
    tokens = resp.json()

    refresh_token = tokens.get("refresh_token")
    if not refresh_token:
        print("No refresh token in response. Try revoking app access and re-running.")
        print(f"Response: {tokens}")
        sys.exit(1)

    # Update bridge config
    config_path = Path(__file__).parent / "bridge_config.json"
    if config_path.exists():
        config = json.loads(config_path.read_text())
    else:
        config = {}

    gc = config.setdefault("google_calendar", {})
    gc["client_id"] = client_id
    gc["client_secret"] = client_secret
    gc["refresh_token"] = refresh_token

    config_path.write_text(json.dumps(config, indent=2))

    print(f"\nRefresh token saved to {config_path}")
    print("The Google Calendar adapter will now work automatically.")


if __name__ == "__main__":
    main()
