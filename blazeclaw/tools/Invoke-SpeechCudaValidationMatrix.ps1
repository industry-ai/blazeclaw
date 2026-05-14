param(
	[string]$LauncherScriptPath = ".\tools\Run-BlazeClawWithAlignedCudaPath.ps1",
	[string]$PolicyScriptPath = ".\tools\Test-SpeechCudaOperationalPolicy.ps1",
	[switch]$IncludePolicyCheck,
	[switch]$IncludeInteractiveCommands
)

$resolvedLauncherPath = [System.IO.Path]::GetFullPath($LauncherScriptPath)
if (-not (Test-Path -LiteralPath $resolvedLauncherPath)) {
	throw "Launcher script not found: $resolvedLauncherPath"
}

$resolvedPolicyScriptPath = [System.IO.Path]::GetFullPath($PolicyScriptPath)
if ($IncludePolicyCheck.IsPresent -and -not (Test-Path -LiteralPath $resolvedPolicyScriptPath)) {
	throw "Policy script not found: $resolvedPolicyScriptPath"
}

$matrix = @(
	[pscustomobject]@{
		Name = "A_known_good_cuda_candidate"
		Description = "Aligned PATH run where CUDA EP may be enabled if stack is compatible."
		DryRunArgs = @("-DryRun")
		InteractiveArgs = @("-WaitForExit")
		Expected = "effectiveProvider=cuda OR explicit CPU reason if guard blocks"
	},
	[pscustomobject]@{
		Name = "B_isolate_non_ort_gpu_consumers"
		Description = "Disable non-ORT GPU users in-process to detect provider conflicts."
		DryRunArgs = @("-IsolateNonOrtGpuConsumers", "-DryRun")
		InteractiveArgs = @("-IsolateNonOrtGpuConsumers", "-WaitForExit")
		Expected = "No cuDNN fail-fast if conflict is caused by non-ORT GPU consumers"
	},
	[pscustomobject]@{
		Name = "C_forced_cpu_safety_gate"
		Description = "Force speech CPU fallback as a safety baseline."
		DryRunArgs = @("-ForceSpeechCpuFallback", "-DryRun")
		InteractiveArgs = @("-ForceSpeechCpuFallback", "-WaitForExit")
		Expected = "startup.runtime.cuda reason=disabled_by_config and stable transcription"
	}
)

Write-Host "Speech CUDA validation matrix (dry-run preflight)" -ForegroundColor Cyan
Write-Host "Launcher: $resolvedLauncherPath"
if ($IncludePolicyCheck.IsPresent) {
	Write-Host "Policy  : $resolvedPolicyScriptPath"
}

$results = New-Object 'System.Collections.Generic.List[object]'

if ($IncludePolicyCheck.IsPresent) {
	Write-Host ""
	Write-Host "[Policy] pinned CUDA/cuDNN policy preflight"
	$policyArgs = @("-ExecutionPolicy", "Bypass", "-File", $resolvedPolicyScriptPath)
	$policyOutput = & powershell @policyArgs 2>&1
	$policyExitCode = $LASTEXITCODE
	$policyStatus = if ($policyExitCode -eq 0) { "pass" } else { "fail" }
	Write-Host "  policy status: $policyStatus (exitCode=$policyExitCode)"
	if ($policyStatus -eq "fail") {
		$policyOutput | ForEach-Object { Write-Host "    $_" }
	}
}

foreach ($entry in $matrix) {
	Write-Host ""
	Write-Host "[$($entry.Name)] $($entry.Description)"

	$argList = @("-ExecutionPolicy", "Bypass", "-File", $resolvedLauncherPath) + $entry.DryRunArgs
	$output = & powershell @argList 2>&1
	$exitCode = $LASTEXITCODE

	$status = if ($exitCode -eq 0) { "pass" } else { "fail" }
	Write-Host "  dry-run status: $status (exitCode=$exitCode)"

	$results.Add([pscustomobject]@{
		Name = $entry.Name
		DryRunStatus = $status
		DryRunExitCode = $exitCode
		Expected = $entry.Expected
		InteractiveCommand = "powershell -ExecutionPolicy Bypass -File `"$resolvedLauncherPath`" " + ($entry.InteractiveArgs -join ' ')
	}) | Out-Null

	if ($status -eq "fail") {
		Write-Host "  output:" -ForegroundColor Yellow
		$output | ForEach-Object { Write-Host "    $_" }
	}
}

Write-Host ""
Write-Host "Matrix summary" -ForegroundColor Cyan
$results | Format-Table Name, DryRunStatus, DryRunExitCode, Expected -AutoSize

if ($IncludeInteractiveCommands.IsPresent) {
	Write-Host ""
	Write-Host "Interactive execution commands" -ForegroundColor Cyan
	$results | ForEach-Object {
		Write-Host "- $($_.Name): $($_.InteractiveCommand)"
	}
}
