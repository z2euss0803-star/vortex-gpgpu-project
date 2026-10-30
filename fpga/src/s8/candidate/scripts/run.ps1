param([ValidateSet('sim','elaborate','hardware','software','verify')] [string]$Stage='verify', [string]$Workspace='')
$ErrorActionPreference='Stop'
$socRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $socRoot
$env:PROCESSOR_ARCHITECTURE='AMD64'
$env:TEMP=Join-Path $socRoot 'build\tmp'
$env:TMP=$env:TEMP
New-Item -ItemType Directory -Force $env:TEMP | Out-Null
switch($Stage) {
  'sim' { & python scripts/sim_ctrl.py; if($LASTEXITCODE){throw 'Control simulation failed'}; & python scripts/sim_gpu.py }
  'elaborate' { & 'G:\xilinx\Vivado\2020.1\bin\vivado.bat' -mode batch -source scripts/finalize_project.tcl -log evidence/finalize_user.log -journal evidence/finalize_user.jou }
  'hardware' { & 'G:\xilinx\Vivado\2020.1\bin\vivado.bat' -mode batch -source scripts/build_hardware.tcl -log evidence/hardware_user.log -journal evidence/hardware_user.jou }
  'software' {
    if(!$Workspace){throw 'Pass a fresh -Workspace path (the existing matching BSP is preserved)'}
    & 'G:\xilinx\Vitis\2020.1\bin\xsct.bat' scripts/build_software.tcl $Workspace
  }
  'verify' { & python scripts/verify.py }
}
if($LASTEXITCODE){throw "Stage $Stage failed; inspect evidence logs"}
