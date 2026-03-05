#!/usr/bin/env bash
# Desktop Command Center — Session Preflight Check
# Validates infrastructure before agent session begins.
# Exit 0 = pass, Exit 2 = blocking failure

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass() { echo -e "${GREEN}✓${NC} $1"; }
warn() { echo -e "${YELLOW}⚠${NC} $1"; }
fail() { echo -e "${RED}✗${NC} $1"; }

BLOCKING=0

echo "─── Desktop Command Center: Preflight ───"

# 1. Check Beads/Dolt connectivity (Pi 5 tunnel)
# Try DCC tunnel ports (3310 primary, 3311-3312 fallback)
TUNNEL_PORT=""
for port in 3310 3311 3312; do
    if bash -c "echo >/dev/tcp/127.0.0.1/$port" 2>/dev/null; then
        TUNNEL_PORT=$port
        break
    fi
done

if [ -n "$TUNNEL_PORT" ]; then
    pass "Pi 5 Dolt tunnel active on port $TUNNEL_PORT"
    # Try a beads command
    if bd ready --limit=1 >/dev/null 2>&1; then
        pass "Beads connectivity OK (Pi 5 Dolt)"
        bd dolt pull 2>/dev/null && pass "Beads synced" || warn "Beads sync failed (non-blocking)"
    else
        fail "Beads command failed — tunnel may be stale"
        BLOCKING=1
    fi
else
    # Try to establish tunnel automatically
    if ssh -fNL 3310:127.0.0.1:3306 strycher@192.168.50.24 2>/dev/null; then
        pass "Pi 5 Dolt tunnel established on port 3310"
        if bd ready --limit=1 >/dev/null 2>&1; then
            pass "Beads connectivity OK (Pi 5 Dolt)"
        else
            fail "Beads command failed after tunnel setup"
            BLOCKING=1
        fi
    else
        fail "No Pi 5 Dolt SSH tunnel on ports 3310-3312"
        warn "Start tunnel: ssh -fNL 3310:127.0.0.1:3306 strycher@192.168.50.24"
        BLOCKING=1
    fi
fi

# 2. Check Agent Mail health
AGENT_MAIL_OK=0
for url in "http://localhost:8912/health/liveness" "https://getunfocused.app/health/liveness"; do
    if curl -sf --max-time 5 "$url" >/dev/null 2>&1; then
        pass "Agent Mail reachable at $url"
        AGENT_MAIL_OK=1
        break
    fi
done

if [ "$AGENT_MAIL_OK" -eq 0 ]; then
    # Try MCP tool check (may be available as MCP server)
    warn "Agent Mail HTTP check failed — will verify via MCP tools"
fi

# 3. Check Git status
cd "$CLAUDE_PROJECT_DIR" 2>/dev/null || cd "$(dirname "$0")/../.."

BRANCH=$(git branch --show-current 2>/dev/null || echo "unknown")
if [ "$BRANCH" = "main" ]; then
    pass "On main branch"
else
    warn "On branch '$BRANCH' (not main)"
fi

if git diff --quiet 2>/dev/null && git diff --cached --quiet 2>/dev/null; then
    pass "Working tree clean"
else
    warn "Uncommitted changes detected"
fi

# 4. Check git hooks path
HOOKSPATH=$(git config core.hookspath 2>/dev/null || echo "")
if [ "$HOOKSPATH" = ".claude/hooks" ]; then
    pass "Git hooks configured"
else
    warn "Git hooks not configured — run: git config core.hookspath .claude/hooks"
fi

echo "─── Preflight complete ───"

if [ "$BLOCKING" -eq 1 ]; then
    fail "BLOCKING: Infrastructure checks failed. Fix issues above before proceeding."
    exit 2
fi

exit 0
