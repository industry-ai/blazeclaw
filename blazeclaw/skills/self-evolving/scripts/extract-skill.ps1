#!/usr/bin/env pwsh
# Skill Extraction Helper (BlazeClaw) - PowerShell
# Creates a new skill scaffold from a learning entry.
# Usage: ./extract-skill.ps1 <skill-name> [--dry-run]

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:SkillsDir = './blazeclaw/skills'

function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Green
}

function Write-Warn {
    param([string]$Message)
    Write-Host "[WARN] $Message" -ForegroundColor Yellow
}

function Write-Err {
    param([string]$Message)
    Write-Host "[ERROR] $Message" -ForegroundColor Red
}

function Show-Usage {
@"
Usage: $(Split-Path -Leaf $PSCommandPath) <skill-name> [options]

Create a new skill scaffold from a learning entry.

Arguments:
  skill-name     Name of the skill (lowercase, hyphens for spaces)

Options:
  --dry-run      Show what would be created without creating files
  --output-dir   Relative output directory under current path (default: ./blazeclaw/skills)
  -h, --help     Show this help message

Examples:
  $(Split-Path -Leaf $PSCommandPath) docker-m1-fixes
  $(Split-Path -Leaf $PSCommandPath) api-timeout-patterns --dry-run
  $(Split-Path -Leaf $PSCommandPath) pnpm-setup --output-dir ./blazeclaw/skills/custom
"@
}

function To-TitleCaseFromSlug {
    param([string]$Slug)
    $words = $Slug -split '-'
    $capitalized = foreach ($w in $words) {
        if ([string]::IsNullOrWhiteSpace($w)) { continue }
        if ($w.Length -eq 1) { $w.ToUpperInvariant() }
        else { $w.Substring(0,1).ToUpperInvariant() + $w.Substring(1).ToLowerInvariant() }
    }
    return ($capitalized -join ' ')
}

$skillName = $null
$dryRun = $false

for ($i = 0; $i -lt $args.Length; $i++) {
    $arg = [string]$args[$i]
    switch ($arg) {
        '--dry-run' {
            $dryRun = $true
            continue
        }
        '--output-dir' {
            if ($i + 1 -ge $args.Length) {
                Write-Err '--output-dir requires a relative path argument'
                Show-Usage
                exit 1
            }
            $i++
            $script:SkillsDir = [string]$args[$i]
            continue
        }
        '-h' {
            Show-Usage
            exit 0
        }
        '--help' {
            Show-Usage
            exit 0
        }
        default {
            if ($arg.StartsWith('-')) {
                Write-Err "Unknown option: $arg"
                Show-Usage
                exit 1
            }

            if ($null -eq $skillName) {
                $skillName = $arg
            } else {
                Write-Err "Unexpected argument: $arg"
                Show-Usage
                exit 1
            }
        }
    }
}

if ([string]::IsNullOrWhiteSpace($skillName)) {
    Write-Err 'Skill name is required'
    Show-Usage
    exit 1
}

if ($skillName -notmatch '^[a-z0-9]+(-[a-z0-9]+)*$') {
    Write-Err 'Invalid skill name format. Use lowercase letters, numbers, and hyphens only.'
    Write-Err "Examples: 'docker-fixes', 'api-patterns', 'pnpm-setup'"
    exit 1
}

if ([System.IO.Path]::IsPathRooted($script:SkillsDir)) {
    Write-Err 'Output directory must be a relative path under the current directory.'
    exit 1
}

if ($script:SkillsDir -match '(^|[\/])\.\.([\/]|$)') {
    Write-Err "Output directory cannot include '..' path segments."
    exit 1
}

$skillsDirNormalized = $script:SkillsDir
if ($skillsDirNormalized.StartsWith('./')) {
    $skillsDirNormalized = $skillsDirNormalized.Substring(2)
}
$skillsDirNormalized = "./$skillsDirNormalized"

$skillPath = "$skillsDirNormalized/$skillName"
$skillDocPath = "$skillPath/SKILL.md"

if ((Test-Path -LiteralPath $skillPath) -and -not $dryRun) {
    Write-Err "Skill already exists: $skillPath"
    Write-Err 'Use a different name or remove the existing skill first.'
    exit 1
}

$title = To-TitleCaseFromSlug -Slug $skillName
$template = @"
---
name: $skillName
description: "[TODO: Add a concise description of what this skill does and when to use it]"
---

# $title

[TODO: Brief introduction explaining the skill's purpose]

## Quick Reference

| Situation | Action |
|-----------|--------|
| [Trigger condition] | [What to do] |

## Usage

[TODO: Detailed usage instructions]

## Examples

[TODO: Add concrete examples]

## Source Learning

This skill was extracted from a learning entry.
- Learning ID: [TODO: Add original learning ID]
- Original File: blazeclaw/skills/self-evolving/.learnings/LEARNINGS.md
"@

if ($dryRun) {
    Write-Info 'Dry run - would create:'
    Write-Host "  $skillPath/"
    Write-Host "  $skillDocPath"
    Write-Host ''
    Write-Host 'Template content would be:'
    Write-Host '---'
    Write-Host $template
    Write-Host '---'
    exit 0
}

Write-Info "Creating skill: $skillName"
New-Item -ItemType Directory -Path $skillPath -Force | Out-Null
Set-Content -LiteralPath $skillDocPath -Value $template -NoNewline

Write-Info "Created: $skillDocPath"
Write-Host ''
Write-Info 'Skill scaffold created successfully!'
Write-Host ''
Write-Host 'Next steps:'
Write-Host "  1. Edit $skillDocPath"
Write-Host '  2. Fill in the TODO sections with content from your learning'
Write-Host '  3. Add references/ folder if you have detailed documentation'
Write-Host '  4. Add scripts/ folder if you have executable code'
Write-Host '  5. Update the original learning entry with:'
Write-Host '     **Status**: promoted_to_skill'
Write-Host "     **Skill-Path**: blazeclaw/skills/$skillName"
