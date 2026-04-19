#requires -Version 5.1
<#
.SYNOPSIS
  Phase B: report duplicate gateway method names registered via `*.Register("method", ...)`.

.DESCRIPTION
  Scans `blazeclaw/BlazeClawMfc/src/gateway` (including `generated/`) for dispatcher
  registrations. The same method may appear in more than one file **intentionally**
  (e.g. `StartLocalRuntimeDispatchOnly` vs full `RegisterDefaultHandlers` — see
  `ToolsSharedHandlers` / gateway docs). Treat this report as a **review aid**, not
  a hard invariant, unless you use `-Strict`.

  Default: exit 0 (informational). With `-Strict`: exit 1 if any duplicate remains
  (use after pruning accidental double-registration).

.PARAMETER RepoRoot
  Workspace root containing `blazeclaw/`. Default: four levels above this script.

.PARAMETER Strict
  Fail the process (exit 1) when duplicates exist.
#>
[CmdletBinding()]
param(
	[string] $RepoRoot = "",
	[switch] $Strict
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
	$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path
}

$scanRoot = Join-Path $RepoRoot "blazeclaw/BlazeClawMfc/src/gateway"
if (-not (Test-Path $scanRoot)) {
	throw "Gateway source not found: $scanRoot"
}

$pattern = [regex]::new('\w+\.Register\(\s*"([^"]+)"')
$byMethod = @{}

Get-ChildItem -Path $scanRoot -Recurse -Include "*.cpp","*.h" -File |
	ForEach-Object {
		$path = $_.FullName
		$text = Get-Content -LiteralPath $path -Raw -ErrorAction SilentlyContinue
		if ($null -eq $text) { return }
		foreach ($m in $pattern.Matches($text)) {
			$name = $m.Groups[1].Value
			if (-not $byMethod.ContainsKey($name)) {
				$byMethod[$name] = New-Object System.Collections.Generic.List[string]
			}
			$rel = $path.Substring($RepoRoot.Length).TrimStart("\")
			$line = "$rel"
			if (-not ($byMethod[$name] -contains $line)) {
				$byMethod[$name].Add($line) | Out-Null
			}
		}
	}

$dupes = @($byMethod.GetEnumerator() | Where-Object { $_.Value.Count -gt 1 } | Sort-Object Name)

if ($dupes.Count -eq 0) {
	Write-Host "OK: no duplicate gateway method registrations under $scanRoot"
	exit 0
}

Write-Host "Review: duplicate method strings ($($dupes.Count) methods appear in multiple files):"
Write-Host "(Some are intentional — e.g. dispatch-only vs full registration; see PROTOCOL_CODEGEN.md / GatewayHost.cpp.md.)"
foreach ($e in $dupes) {
	Write-Host ""
	Write-Host "  $($e.Name) ($($e.Value.Count) files):"
	foreach ($p in $e.Value) {
		Write-Host "    $p"
	}
}
if ($Strict) {
	exit 1
}
exit 0
