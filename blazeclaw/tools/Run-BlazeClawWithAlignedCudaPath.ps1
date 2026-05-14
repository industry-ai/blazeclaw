param(
	[string]$ExePath = "./bin/Debug/BlazeClaw.exe",
	[string]$CudaBinPath = "D:\Program Files\bin\x64",
	[string]$CudnnBinPath = "D:\nVidia\cudnn9.20\bin\12.9\x64",
	[switch]$IsolateNonOrtGpuConsumers,
	[switch]$ForceSpeechCpuFallback,
	[switch]$DryRun,
	[switch]$WaitForExit
)

$resolvedExePath = [System.IO.Path]::GetFullPath($ExePath)
if (-not (Test-Path -LiteralPath $resolvedExePath)) {
	throw "BlazeClaw executable not found: $resolvedExePath"
}

$resolvedCudaBinPath = [System.IO.Path]::GetFullPath($CudaBinPath)
$resolvedCudnnBinPath = [System.IO.Path]::GetFullPath($CudnnBinPath)

if (-not (Test-Path -LiteralPath $resolvedCudaBinPath)) {
	throw "CUDA bin path not found: $resolvedCudaBinPath"
}

if (-not (Test-Path -LiteralPath $resolvedCudnnBinPath)) {
	throw "cuDNN bin path not found: $resolvedCudnnBinPath"
}

function Normalize-PathString {
	param([string]$Value)

	if ([string]::IsNullOrWhiteSpace($Value)) {
		return ""
	}

	$normalized = $Value.Trim().Trim('"')
	return $normalized.TrimEnd('\\')
}

function Upsert-ConfigEntry {
	param(
		[string[]]$Lines,
		[string]$Key,
		[string]$Value
	)

	$prefix = "$Key="
	for ($i = 0; $i -lt $Lines.Count; $i++) {
		$line = $Lines[$i].Trim()
		if ($line.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
			$Lines[$i] = "$prefix$Value"
			return ,$Lines
		}
	}

	return ,($Lines + "$prefix$Value")
}

$originalEntries = @($env:Path -split ';' | ForEach-Object { Normalize-PathString $_ } | Where-Object { $_ -ne "" })

$filteredEntries = @()
foreach ($entry in $originalEntries) {
	$entryLower = $entry.ToLowerInvariant()
	$isConflicting = $false

	if ($entryLower -eq $resolvedCudaBinPath.ToLowerInvariant().TrimEnd('\\')) {
		$isConflicting = $true
	}
	elseif ($entryLower -eq $resolvedCudnnBinPath.ToLowerInvariant().TrimEnd('\\')) {
		$isConflicting = $true
	}
	elseif ($entryLower -match "\\nvidia\\cudnn") {
		$isConflicting = $true
	}
	elseif ($entryLower -match "\\nvidia gpu computing toolkit\\cuda\\") {
		$isConflicting = $true
	}
	elseif ($entryLower -match "\\program files\\bin\\x64$") {
		$isConflicting = $true
	}

	if (-not $isConflicting) {
		$filteredEntries += $entry
	}
}

$exeDirectory = Split-Path -Path $resolvedExePath -Parent
$alignedEntries = @(
	Normalize-PathString $exeDirectory
	Normalize-PathString $resolvedCudaBinPath
	Normalize-PathString $resolvedCudnnBinPath
) + $filteredEntries

$uniqueEntries = New-Object 'System.Collections.Generic.List[string]'
$seen = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
foreach ($entry in $alignedEntries) {
	if ([string]::IsNullOrWhiteSpace($entry)) {
		continue
	}

	if ($seen.Add($entry)) {
		[void]$uniqueEntries.Add($entry)
	}
}

$alignedPath = ($uniqueEntries -join ';')

Write-Host "Launching BlazeClaw with aligned CUDA PATH"
Write-Host "  Exe      : $resolvedExePath"
Write-Host "  CUDA bin : $resolvedCudaBinPath"
Write-Host "  cuDNN bin: $resolvedCudnnBinPath"
Write-Host "  PATH entries (new): $($uniqueEntries.Count)"

if ($IsolateNonOrtGpuConsumers.IsPresent) {
	if (-not $WaitForExit.IsPresent -and -not $DryRun.IsPresent) {
		throw "-IsolateNonOrtGpuConsumers requires -WaitForExit so temporary config changes can be restored automatically."
	}

	Write-Host "  Isolation: enabled (temporary non-ORT GPU consumers disabled for this run)"
}

if ($ForceSpeechCpuFallback.IsPresent) {
	if (-not $WaitForExit.IsPresent -and -not $DryRun.IsPresent) {
		throw "-ForceSpeechCpuFallback requires -WaitForExit so temporary config changes can be restored automatically."
	}

	Write-Host "  Speech CPU safety gate: enabled (speech.cuda.enabled=false for this run)"
}

if ($DryRun.IsPresent) {
	Write-Host "DryRun enabled. Process launch skipped."
	return
}

$configBackupPath = $null
$resolvedConfigPath = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\blazeclaw.conf"))

if ($IsolateNonOrtGpuConsumers.IsPresent) {
	if (-not (Test-Path -LiteralPath $resolvedConfigPath)) {
		throw "Configuration file not found for isolation run: $resolvedConfigPath"
	}

	$configBackupPath = "$resolvedConfigPath.step4-isolation.bak"
	Copy-Item -LiteralPath $resolvedConfigPath -Destination $configBackupPath -Force

	$configLines = Get-Content -LiteralPath $resolvedConfigPath
	$configLines = Upsert-ConfigEntry -Lines $configLines -Key "chat.localModel.enabled" -Value "false"
	$configLines = Upsert-ConfigEntry -Lines $configLines -Key "chat.localModel.llama.gpuLayers" -Value "0"
	Set-Content -LiteralPath $resolvedConfigPath -Value $configLines -Encoding UTF8

	Write-Host "  Isolation config applied: chat.localModel.enabled=false, chat.localModel.llama.gpuLayers=0"
}

if ($ForceSpeechCpuFallback.IsPresent) {
	if (-not (Test-Path -LiteralPath $resolvedConfigPath)) {
		throw "Configuration file not found for speech CPU safety run: $resolvedConfigPath"
	}

	if ($configBackupPath -eq $null) {
		$configBackupPath = "$resolvedConfigPath.step5-cpu-fallback.bak"
		Copy-Item -LiteralPath $resolvedConfigPath -Destination $configBackupPath -Force
	}

	$configLines = Get-Content -LiteralPath $resolvedConfigPath
	$configLines = Upsert-ConfigEntry -Lines $configLines -Key "speech.cuda.enabled" -Value "false"
	Set-Content -LiteralPath $resolvedConfigPath -Value $configLines -Encoding UTF8

	Write-Host "  Speech safety config applied: speech.cuda.enabled=false"
}

$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = $resolvedExePath
$startInfo.WorkingDirectory = $exeDirectory
$startInfo.UseShellExecute = $false
$startInfo.EnvironmentVariables["PATH"] = $alignedPath

try {
	$process = New-Object System.Diagnostics.Process
	$process.StartInfo = $startInfo

	if (-not $process.Start()) {
		throw "Failed to start BlazeClaw with aligned PATH."
	}

	Write-Host "BlazeClaw started. PID=$($process.Id)"

	if ($WaitForExit.IsPresent) {
		$process.WaitForExit()
		Write-Host "BlazeClaw exited with code $($process.ExitCode)"
	}
}
finally {
	if ($configBackupPath -ne $null -and (Test-Path -LiteralPath $configBackupPath)) {
		Move-Item -LiteralPath $configBackupPath -Destination $resolvedConfigPath -Force
		Write-Host "Isolation config restored from backup."
	}
}
