# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in this project, please report it
responsibly:

1. **Do NOT open a public GitHub issue.**
2. Email the maintainer or use GitHub's private vulnerability reporting:
   **Settings → Security → Advisories → Report a vulnerability**
3. Include steps to reproduce and any relevant details.

## Scope

This project runs on a local network with no internet-facing services.
The primary security concerns are:

- **API tokens** (Home Assistant, GitHub, Google Calendar) stored in `.env`
- **Bridge service** (FastAPI) accessible on the local network
- **Firmware** communicates with the bridge over HTTP (no TLS on LAN)

## Supported Versions

Only the latest version on `main` is supported. There are no LTS branches.

## Best Practices for Users

- Never commit `.env`, `bridge_config.json`, or `client_secret.json`
- Use the pre-commit hook: `git config core.hooksPath .githooks`
- Rotate tokens if you suspect they've been exposed
- Run the bridge behind a firewall (it's designed for LAN-only use)
