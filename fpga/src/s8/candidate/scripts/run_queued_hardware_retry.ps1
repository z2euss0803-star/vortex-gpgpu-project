# Retry only the failed hardware/software stages; retain the passed reset test.
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $taskRoot
$env:PROCESSOR_ARCHITECTURE='AMD64'
function Save-RetryState([string]$stage,[string]$status,[string]$detail) {
  [pscustomobject]@{pid=$PID;stage=$stage;status=$status;detail=$detail;updated=(Get-Date -Format o)} | ConvertTo-Json | Set-Content -LiteralPath 'evidence/pipeline_state.json'
}
try {
  $state=Get-Content -LiteralPath 'evidence/pipeline_state.json' -Raw | ConvertFrom-Json
  if($state.status -ne 'failed'){throw 'Retry requires a preserved failed predecessor'}
  Copy-Item -LiteralPath 'evidence/pipeline_state.json' -Destination 'evidence/attempt_01_hardware_main_exit/state_after_cleanup.json'
  while($true) {
    $mem=Get-CimInstance Win32_OperatingSystem
    Save-RetryState 'queued_hardware_memory' 'waiting' ("free_commit_KiB={0}; require >=12GiB; general/synth threads=2; passed reset simulation reused" -f $mem.FreeVirtualMemory)
    if($mem.FreeVirtualMemory -ge 12582912){break}
    Start-Sleep -Seconds 180
  }
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_pipeline.ps1 -SkipSimulation -SoftwareWorkspace "$taskRoot\build\vitis_dual_r2"
  if($LASTEXITCODE){throw 'Hardware retry pipeline failed; preserve current logs before further work'}
} catch {
  $_ | Out-String | Add-Content -LiteralPath 'evidence/hardware_retry_error.log'
  Save-RetryState 'stopped' 'failed' ([string]$_)
  exit 1
}
