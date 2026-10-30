param([Parameter(Mandatory=$true)][string]$BundleRoot,[Parameter(Mandatory=$true)][string]$BinDir)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$env:PROCESSOR_ARCHITECTURE='AMD64'
$state=[ordered]@{phase='preflight';result='WAIT_RTL';start=[DateTime]::UtcNow.ToString('o');steps=@()}
try {
 Set-Location -LiteralPath $BundleRoot
 $memory=Get-CimInstance Win32_OperatingSystem
 $state.memory=@{physical_kib=$memory.FreePhysicalMemory;virtual_kib=$memory.FreeVirtualMemory}
 if($memory.FreePhysicalMemory -lt 4194304 -or $memory.FreeVirtualMemory -lt 8388608){throw 'WAIT_RESOURCE'}
 function Step([string]$Name,[string]$Tool,[string[]]$Arguments,[bool]$AllowFailure=$false) {
  $start=[DateTime]::UtcNow.ToString('o')
  $log=Join-Path $BundleRoot ($Name+'.console.log')
  $command=@((Join-Path $BinDir ($Tool+'.bat')))+$Arguments
  $command | ConvertTo-Json | Set-Content -LiteralPath ($Name+'-command.json')
  # Quote every token through cmd, including testplusarg strings containing '='.
  # Unquoted '=' is a cmd/batch argument delimiter before XSim can see it.
  $quoted=($command | ForEach-Object {'"'+$_.Replace('"','""')+'"'}) -join ' '
  & $env:ComSpec /d /s /c ('"'+$quoted+'"') *> $log
  $natural=$LASTEXITCODE
  $record=@{name=$Name;start=$start;end=[DateTime]::UtcNow.ToString('o');natural_exit=$natural;log=$log;log_sha256=(Get-FileHash -LiteralPath $log -Algorithm SHA256).Hash.ToLowerInvariant();command_file=($Name+'-command.json');command_sha256=(Get-FileHash -LiteralPath ($Name+'-command.json') -Algorithm SHA256).Hash.ToLowerInvariant()}
  $script:state.steps+=@($record)
  $script:state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath 'state.json'
  if($natural -ne 0 -and -not $AllowFailure){throw ($Name+'_EXIT_'+$natural)}
 }
 Step 'xvlog' 'xvlog' @('--relax','--prj','gpu.prj')
 Step 'xvhdl' 'xvhdl' @('--relax','--prj','ip.prj')
 Step 'xelab' 'xelab' @('--relax','--debug','typical','--mt','2','-L','floating_point_v7_1_10','-L','xil_defaultlib','-L','unisims_ver','-L','unimacro_ver','-L','secureip','xil_defaultlib.tb_hunet_fused','-s','fused')
 $state.phase='simulation'
 $cases=Get-Content -LiteralPath 'cases.json' -Raw | ConvertFrom-Json
 foreach($case in $cases){
  $opts=@([System.IO.File]::ReadAllLines((Join-Path $BundleRoot ($case.name+'/options.txt'))) | Where-Object {$_ -ne '--runall'})
  $wave=Join-Path $BundleRoot ($case.name+'/fused.vcd')
  $wdb=Join-Path $BundleRoot ($case.name+'/fused.wdb')
  if(Test-Path -LiteralPath 'fused.vcd'){throw 'UNHARVESTED_WAVE'}
  Step ('xsim-'+$case.name) 'xsim' (@('fused','--runall','--onerror','quit','--onfinish','quit','--wdb',$wdb)+$opts) ($case.name -eq 'memory_fault')
  if(-not (Test-Path -LiteralPath 'fused.vcd') -or (Get-Item -LiteralPath 'fused.vcd').Length -eq 0){throw 'WAVE_NOT_CAPTURED'}
  Move-Item -LiteralPath 'fused.vcd' -Destination $wave
  $state.steps[-1].wave=$wave
  $state.steps[-1].wave_sha256=(Get-FileHash -LiteralPath $wave -Algorithm SHA256).Hash.ToLowerInvariant()
  $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath 'state.json'
  $log=Get-Content -LiteralPath ('xsim-'+$case.name+'.console.log') -Raw
  if($case.name -eq 'memory_fault'){
   if($log -notmatch 'FUSED_MEMORY_FAULT_INJECTED' -or $log -notmatch 'RTL_CYCLE_TIMEOUT' -or $log -match 'FUSED_MULTICORE_RTL_PASS'){throw 'FAULT_NOT_REJECTED'}
  }else{
   if($log -notmatch 'FUSED_MULTICORE_RTL_PASS' -or $log -notmatch 'FUSED_TB_RESET_BOOTSTRAP_RELEASED' -or $log -match '(?im)^\s*Error:|fatal|assertion failed|timeout'){throw ('SEMANTIC_FAIL_'+$case.name)}
   if($case.name -eq 'reuse_b' -and $log -notmatch 'FUSED_TWO_LAUNCH_REUSE_PASS'){throw 'REUSE_NOT_EXECUTED'}
  }
 }
 $state.result='RTL_SUITE_COMPLETED_REQUIRES_INDEPENDENT_AUDIT'
}catch{$state.error=$_.Exception.Message}
finally{
 $state.end=[DateTime]::UtcNow.ToString('o')
 $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $BundleRoot 'exit.json')
}
if($state.result -eq 'RTL_SUITE_COMPLETED_REQUIRES_INDEPENDENT_AUDIT'){exit 0}else{exit 1}
