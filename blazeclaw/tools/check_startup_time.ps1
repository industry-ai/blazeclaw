param(
	[int]$ThresholdMs = 5000,
	[int]$DurationSec = 12,
	[string]$OutputDir = ".\traces\ci"
)

$ErrorActionPreference = 'Stop'

Write-Output "Startup timing check: threshold=${ThresholdMs}ms duration=${DurationSec}s output=${OutputDir}"

# Ensure output dir
if (-not (Test-Path $OutputDir)) {
	New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

# Locate collector
$collector = Get-ChildItem -Path . -Filter collect_startup_traces.ps1 -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if (-not $collector) {
	Write-Error "collect_startup_traces.ps1 not found"
	exit 2
}

# Run collector
Write-Output "Running collector: $collector"
& powershell -NoProfile -ExecutionPolicy Bypass -File $collector -OutputDir $OutputDir -DurationSec $DurationSec

# Find latest trace file
$traceFile = Get-ChildItem -Path $OutputDir -Filter "*.trace.*.log" -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $traceFile) {
	Write-Error "No trace file produced in $OutputDir"
	exit 3
}

Write-Output "Parsing trace: $($traceFile.FullName)"

# Read trace and find begin/completed for the same pid (prefer latest pid)
$content = Get-Content -Path $traceFile.FullName -ErrorAction Stop

# Parse lines like: pid=<pid> tick=<tick> stage=<stage>
$pattern = 'pid=(\d+) tick=(\d+) stage=(.+)'
$records = @()
foreach ($line in $content) {
	if ($line -match $pattern) {
		$records += [PSCustomObject]@{
			pid = [int]$Matches[1]
			tick = [long]$Matches[2]
			stage = $Matches[3]
			raw = $line
		}
	}
}

if ($records.Count -eq 0) {
	Write-Error "No checkpoint records parsed from trace"
	exit 4
}

# Pick latest pid by most recent tick
$grouped = $records | Group-Object -Property pid
$best = $null
$bestPid = $null
$bestMaxTick = -1
foreach ($g in $grouped) {
	$maxTick = ($g.Group | Measure-Object -Property tick -Maximum).Maximum
	if ($maxTick -gt $bestMaxTick) {
		$bestMaxTick = $maxTick
		$best = $g.Group
		$bestPid = $g.Name
	}
}

$begin = $best | Where-Object { $_.stage -eq 'InitInstance.begin' } | Select-Object -First 1
$completed = $best | Where-Object { $_.stage -eq 'InitInstance.completed.true' } | Select-Object -First 1

if (-not $begin -or -not $completed) {
	Write-Output "Begin or completed stage not found for pid $bestPid. Stages present: $(( $best | Select-Object -ExpandProperty stage ) -join ',')"
	exit 5
}

$delta = [int]($completed.tick - $begin.tick)
Write-Output "Startup delta for pid $bestPid = ${delta} ms (threshold ${ThresholdMs} ms)"

if ($delta -le $ThresholdMs) {
	Write-Output "Startup time within threshold"
	exit 0
}
else {
	Write-Error "Startup time exceeded threshold: ${delta} ms > ${ThresholdMs} ms"
	exit 6
}
