param([switch]$SkipSimulation,[switch]$ResumeSynthesis,[switch]$ResumeRouted,[switch]$ExportOnly,[switch]$SoftwareOnly,[string]$SoftwareWorkspace='')
$ErrorActionPreference='Stop'
$socRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $socRoot
$python='C:\Users\13151\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$env:PROCESSOR_ARCHITECTURE='AMD64'
$env:TEMP=Join-Path $socRoot 'build\tmp'
$env:TMP=$env:TEMP
$env:V23_XELAB_O0='1'
$stateFile=Join-Path $socRoot 'evidence\pipeline_state.json'
function Save-PipelineState([string]$stage,[string]$status,[int]$code=0) {
  [pscustomobject]@{pid=$PID;stage=$stage;status=$status;exit_code=$code;updated=(Get-Date -Format o)} | ConvertTo-Json | Set-Content -LiteralPath $stateFile
}
try {
  Save-PipelineState 'generated_audit' 'running'
  & $python scripts/audit_generated.py *> evidence/generated_audit.log
  if($LASTEXITCODE){throw 'Generated hardware audit failed'}
  & $python scripts/freeze_inputs.py *> evidence/pipeline_inputs.log
  if($LASTEXITCODE){throw 'Input manifest failed'}
  if(!$SkipSimulation -and !$SoftwareOnly) {
    Save-PipelineState 'simulation' 'running'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_job.ps1 -Stage sim
    if($LASTEXITCODE){throw 'GPU simulation failed'}
  }
  if(!$SoftwareOnly) {
    Save-PipelineState 'hardware' 'running'
    $hardwareStage=if($ExportOnly){'hardware_export'}elseif($ResumeRouted){'hardware_cdc'}elseif($ResumeSynthesis){'hardware_resume'}else{'hardware'}
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_job.ps1 -Stage $hardwareStage
    if($LASTEXITCODE){throw 'Hardware synthesis/implementation failed'}
  }
  Save-PipelineState 'software' 'running'
  $xsa=Join-Path $socRoot 'build\artifacts\gpgpu_with_bit.xsa'
  $swArgs=@('build_dual_software.tcl','software_build.log',$xsa)
  if($SoftwareWorkspace){$swArgs+=$SoftwareWorkspace}
  & $python scripts/run_xsct_job.py @swArgs
  if($LASTEXITCODE){throw 'Matching BSP/ELF build failed'}
  Save-PipelineState 'delivery_audit' 'running'
  & $python scripts/audit_delivery.py *> evidence/delivery_audit.log
  if($LASTEXITCODE){throw 'Delivery evidence/hash audit failed'}
  Save-PipelineState 'complete' 'passed'
} catch {
  $_ | Out-String | Add-Content -LiteralPath evidence/pipeline_error.log
  Save-PipelineState 'stopped' 'failed' 1
  exit 1
}
