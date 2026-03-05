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

# 1. Check Beads/Dolt connectivity
# Try common tunnel ports
TUNNEL_PORT=""
for port in 3307 3308 3309; do
    if bash -c "echo >/dev/tcp/127.0.0.1/$port" 2>/dev/null; then
        TUNNEL_PORT=$port
        break
    fi
done

if [ -n "$TUNNEL_PORT" ]; then
    pass "Dolt tunnel active on port $TUNNEL_PORT"
    # Try a beads command
    if bd ready --limit=1 >/dev/null 2>&1; then
        pass "Beads connectivity OK"
        bd dolt pull 2>/dev/null && pass "Beads synced" || warn "Beads sync failed (non-blocking)"
    else
        fail "Beads command failed — tunnel may be stale"
        BLOCKING=1
    fi
else
    fail "No Dolt SSH tunnel found on ports 3307-3309"
    warn "Start tunnel: ssh -fNL 3307:127.0.0.1:3307 unfocused@46.224.181.82"
    BLOCKING=1
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
