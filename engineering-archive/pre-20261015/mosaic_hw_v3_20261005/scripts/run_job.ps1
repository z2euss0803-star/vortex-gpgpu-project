param([ValidateSet('prepare','resume','hardware','hardware_resume','hardware_cdc','hardware_export','implement','sim')] [string]$Stage='prepare')
$ErrorActionPreference='Stop'
$socRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $socRoot
$env:PROCESSOR_ARCHITECTURE='AMD64'
$env:TEMP=Join-Path $socRoot 'build\tmp'
$env:TMP=$env:TEMP
New-Item -ItemType Directory -Force -Path $env:TEMP | Out-Null
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss'
$log=Join-Path $socRoot "evidence\${Stage}_${stamp}.log"
$journal=Join-Path $socRoot "evidence\${Stage}_${stamp}.jou"
if($Stage -eq 'sim') {
  & 'C:\Users\13151\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' scripts/sim_gpu.py *> $log
} else {
  $script=@{prepare='prepare_dual.tcl';resume='resume_dual.tcl';hardware='build_hardware.tcl';hardware_resume='resume_synthesis.tcl';hardware_cdc='resume_routed_cdc.tcl';hardware_export='resume_export.tcl';implement='implement_checkpoint.tcl'}[$Stage]
  # A file-backed child stdout lets the launcher observe main-process exit even
  # when orphaned synthesis workers still hold inherited output handles.
  & 'C:\Users\13151\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' scripts/run_vivado_job.py $script $log $journal
}
$code=$LASTEXITCODE
[pscustomobject]@{stage=$Stage;exit_code=$code;finished=(Get-Date -Format o);log=$log} | ConvertTo-Json | Set-Content -LiteralPath "evidence\${Stage}_${stamp}.exit.json"
exit $code
