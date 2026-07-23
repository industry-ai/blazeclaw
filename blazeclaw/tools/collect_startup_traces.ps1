param(
	[string]$OutputDir = ".\\traces",
	[int]$DurationSec = 20,
	[string]$BlazeClawExe = ".\\bin\\Debug\\BlazeClaw.exe",
	[string]$BlazeClawArgs = "-RunForStartupBench --RecordStartupTrace"
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $OutputDir)) {
	New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

$timestamp = Get-Date -Format yyyyMMddHHmmss
$outEtl = Join-Path $OutputDir ("BlazeClaw.startup.$timestamp.etl")

# Resolve BlazeClaw exe
if (-not (Test-Path $BlazeClawExe)) {
	Write-Host "Provided BlazeClaw exe path not found: $BlazeClawExe"
	$found = Get-ChildItem -Path . -Filter BlazeClaw.exe -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
	if ($found) {
		$BlazeClawExe = $found.FullName
		Write-Host "Found BlazeClaw at: $BlazeClawExe"
	} else {
		Write-Error "Unable to locate BlazeClaw.exe. Provide correct path via -BlazeClawExe"
		exit 2
	}
}

$wpr = Join-Path $env:windir "System32\wpr.exe"
$wprAvailable = Test-Path $wpr

Write-Host "Output folder: $OutputDir"
Write-Host "Trace duration (sec): $DurationSec"
Write-Host "BlazeClaw exe: $BlazeClawExe"
Write-Host "WPR available: $wprAvailable"

if ($wprAvailable) {
	Write-Host "Starting WPR GeneralProfile (requires admin)."
	& $wpr -start GeneralProfile -filemode
} else {
	Write-Host "WPR not available on this machine. Skipping ETW capture."
}

# Start BlazeClaw and wait
Write-Host "Starting BlazeClaw..."
$proc = Start-Process -FilePath $BlazeClawExe -ArgumentList $BlazeClawArgs -PassThru

if ($DurationSec -gt 0) {
	Write-Host "Sleeping $DurationSec seconds to capture startup window..."
	Start-Sleep -Seconds $DurationSec
}

# Attempt to stop process if it is still running (best-effort for automated runs)
if (!$proc.HasExited) {
	try {
		Write-Host "Waiting for process to exit (timeout $DurationSec sec)..."
		$proc | Wait-Process -Timeout 5 | Out-Null
	} catch {
		# ignore
	}
}

if ($wprAvailable) {
	Write-Host "Stopping WPR and saving ETL to: $outEtl"
	& $wpr -stop $outEtl
}

# Collect app-generated traces (startup checkpoint and io logs)
$temp = $env:TEMP
$traceSrc = Join-Path $temp "BlazeClaw.startup.trace.log"
$ioSrc = Join-Path $temp "BlazeClaw.startup.io.log"

if (Test-Path $traceSrc) {
	Copy-Item -Path $traceSrc -Destination (Join-Path $OutputDir ("BlazeClaw.startup.trace.$timestamp.log")) -Force
	Write-Host "Copied startup checkpoint trace"
}
if (Test-Path $ioSrc) {
	Copy-Item -Path $ioSrc -Destination (Join-Path $OutputDir ("BlazeClaw.startup.io.$timestamp.log")) -Force
	Write-Host "Copied startup IO trace"
}

Write-Host "Trace collection complete. Artifacts in: $OutputDir"
Write-Host "ETL (if collected): $outEtl"
