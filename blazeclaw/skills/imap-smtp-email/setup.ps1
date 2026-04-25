#Requires -Version 5.1
<#
.SYNOPSIS
  Installs Node dependencies for the imap-smtp-email skill on Windows (Phase B bootstrap).

.DESCRIPTION
  Run from the skill directory (same folder as package.json).
  For interactive email credential setup, use Git Bash with setup.sh or the BlazeClaw config UI (config.html).
#>
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here

if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    Write-Host "Node.js was not found on PATH. Install Node.js LTS, or set BLAZECLAW_NODE_PATH to the full path of node.exe so BlazeClaw can spawn the skill." -ForegroundColor Red
    exit 1
}

if (-not (Test-Path "package-lock.json")) {
    Write-Host "package-lock.json not found; running npm install..." -ForegroundColor Yellow
    npm install
} else {
    npm ci
}

Write-Host "imap-smtp-email: npm dependencies installed." -ForegroundColor Green
Write-Host "Next: configure ~/.config/imap-smtp-email/.env (see SKILL.md) or use config.html from BlazeClaw."
