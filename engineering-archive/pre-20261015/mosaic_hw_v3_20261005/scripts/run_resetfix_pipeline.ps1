$ErrorActionPreference='Stop'
$resetRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $resetRoot
$env:PROCESSOR_ARCHITECTURE='AMD64'
$resetPython='C:\Users\13151\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
function Save-ResetState([string]$stage,[string]$status) {
  [pscustomobject]@{pid=$PID;stage=$stage;status=$status;updated=(Get-Date -Format o)} | ConvertTo-Json | Set-Content -LiteralPath 'evidence/pipeline_state.json'
}
try {
  Save-ResetState 'prepare_resetfix' 'running'
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_job.ps1 -Stage prepare
  if($LASTEXITCODE){throw 'Resetfix preparation failed'}
  Save-ResetState 'reset_release_simulation' 'running'
  & $resetPython scripts/test_reset_release.py *> evidence/reset_release_driver.log
  if($LASTEXITCODE){throw 'Actual reset-release simulation failed'}
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_pipeline.ps1 -SkipSimulation -SoftwareWorkspace "$resetRoot\build\vitis_dual_r2"
  if($LASTEXITCODE){throw 'Resetfix hardware/software pipeline failed'}
} catch {
  $_ | Out-String | Add-Content -LiteralPath evidence/resetfix_pipeline_error.log
  Save-ResetState 'stopped' 'failed'
  exit 1
}
