# Desktop Command Center — Claude Code Session Launcher
# Validates infrastructure then launches Claude Code in interactive mode.
# Usage: .\scripts\start-claude.ps1

$ErrorActionPreference = "Continue"

Write-Host "─── Desktop Command Center: Session Launcher ───" -ForegroundColor Cyan

$blocking = $false

# --- 1. Git status ---
Push-Location $PSScriptRoot\..
$branch = git branch --show-current 2>$null
if ($branch -eq "main") {
    Write-Host "  ✓ On main branch" -ForegroundColor Green
    # Pull latest
    git pull --ff-only origin main 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  ✓ Main branch up to date" -ForegroundColor Green
    } else {
        Write-Host "  ⚠ Could not pull latest main (non-blocking)" -ForegroundColor Yellow
    }
} else {
    Write-Host "  ⚠ On branch '$branch' — consider switching to main first" -ForegroundColor Yellow
}

# Check for uncommitted changes
$status = git status --porcelain 2>$null
if ($status) {
    Write-Host "  ⚠ Uncommitted changes detected" -ForegroundColor Yellow
} else {
    Write-Host "  ✓ Working tree clean" -ForegroundColor Green
}

# --- 2. SSH Tunnel / Pi 5 Dolt ---
$tunnelPort = $null
foreach ($port in @(3310, 3311, 3312)) {
    try {
        $tcp = New-Object System.Net.Sockets.TcpClient
        $tcp.Connect("127.0.0.1", $port)
        $tcp.Close()
        $tunnelPort = $port
        break
    } catch {
        continue
    }
}

if ($tunnelPort) {
    Write-Host "  ✓ Pi 5 Dolt tunnel active on port $tunnelPort" -ForegroundColor Green
    # Test Beads
    $bdResult = bd ready --limit=1 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  ✓ Beads connectivity OK (Pi 5 Dolt)" -ForegroundColor Green
        bd dolt pull 2>$null | Out-Null
        Write-Host "  ✓ Beads synced" -ForegroundColor Green
    } else {
        Write-Host "  ✗ Beads command failed — tunnel may be stale" -ForegroundColor Red
        $blocking = $true
    }
} else {
    # Try to establish tunnel automatically
    Write-Host "  ⚠ No Pi 5 Dolt tunnel — attempting to establish..." -ForegroundColor Yellow
    $sshResult = Start-Process -FilePath "ssh" -ArgumentList "-fNL 3310:127.0.0.1:3306 strycher@192.168.50.24" -NoNewWindow -PassThru -Wait
    Start-Sleep -Seconds 2
    try {
        $tcp = New-Object System.Net.Sockets.TcpClient
        $tcp.Connect("127.0.0.1", 3310)
        $tcp.Close()
        Write-Host "  ✓ Pi 5 Dolt tunnel established on port 3310" -ForegroundColor Green
        $bdResult = bd ready --limit=1 2>$null
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  ✓ Beads connectivity OK (Pi 5 Dolt)" -ForegroundColor Green
        } else {
            Write-Host "  ✗ Beads command failed after tunnel setup" -ForegroundColor Red
            $blocking = $true
        }
    } catch {
        Write-Host "  ✗ Could not establish Pi 5 Dolt tunnel" -ForegroundColor Red
        Write-Host "    Start manually: ssh -fNL 3310:127.0.0.1:3306 strycher@192.168.50.24" -ForegroundColor Yellow
        $blocking = $true
    }
}

# --- 3. Agent Mail ---
$agentMailOk = $false
foreach ($url in @("http://localhost:8912/health/liveness", "https://getunfocused.app/health/liveness")) {
    try {
        $response = Invoke-WebRequest -Uri $url -TimeoutSec 5 -UseBasicParsing -ErrorAction Stop
        if ($response.StatusCode -eq 200) {
            Write-Host "  ✓ Agent Mail reachable" -ForegroundColor Green
            $agentMailOk = $true
            break
        }
    } catch {
        continue
    }
}

if (-not $agentMailOk) {
    Write-Host "  ⚠ Agent Mail HTTP check failed — will verify via MCP" -ForegroundColor Yellow
}

# --- 4. Git hooks ---
$hookspath = git config core.hookspath 2>$null
if ($hookspath -eq ".claude/hooks") {
    Write-Host "  ✓ Git hooks configured" -ForegroundColor Green
} else {
    Write-Host "  ⚠ Setting git hooks path..." -ForegroundColor Yellow
    git config core.hookspath .claude/hooks
    Write-Host "  ✓ Git hooks configured" -ForegroundColor Green
}

Pop-Location

# --- Result ---
Write-Host ""
if ($blocking) {
    Write-Host "  ✗ BLOCKING: Fix infrastructure issues above before proceeding." -ForegroundColor Red
    Write-Host ""
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host "─── All checks passed. Launching Claude Code ───" -ForegroundColor Green
Write-Host ""
Write-Host "  Tip: Type /work to enter autonomous Worker Mode" -ForegroundColor Cyan
Write-Host "  Tip: Stay in Interactive Mode for planning/design" -ForegroundColor Cyan
Write-Host ""

# Launch Claude Code
Set-Location $PSScriptRoot\..
claude --dangerously-skip-permissions
