param([UInt32]$TargetWorker=0,[string]$Snapshot='',[string]$OutputPath='',[string]$TrackedPath='')
$ErrorActionPreference='Stop'
$all=Get-CimInstance Win32_Process
$heavy=@($all | Where-Object {$_.Name -match '^(vivado|xsim|xsimk|xelab|xvlog|xvhdl|xsc)\.exe$'})
$ids=[System.Collections.Generic.HashSet[UInt32]]::new()
if($TargetWorker){[void]$ids.Add($TargetWorker);if(!$Snapshot){throw 'Bound worker query requires exact snapshot'}}
$tracked=@()
if($TrackedPath -and (Test-Path -LiteralPath $TrackedPath)){
    $tracked=@((Get-Content -LiteralPath $TrackedPath -Raw | ConvertFrom-Json).receipts)
    foreach($receipt in $tracked){
        foreach($taskProcess in $all){
            if($taskProcess.ProcessId -eq $receipt.ProcessId -and $taskProcess.CreationDate.ToString('o') -eq $receipt.CreationDate){[void]$ids.Add([UInt32]$taskProcess.ProcessId)}
        }
    }
}
do{
    $added=$false
    foreach($taskProcess in $all){if($TargetWorker -and $ids.Contains([UInt32]$taskProcess.ParentProcessId) -and !$ids.Contains([UInt32]$taskProcess.ProcessId)){
        [void]$ids.Add([UInt32]$taskProcess.ProcessId);$added=$true
    }}
}while($added)
$selected=@($all | Where-Object {$ids.Contains([UInt32]$_.ProcessId) -or $_.Name -match '^(vivado|xsim|xsimk|xelab|xvlog|xvhdl|xsc)\.exe$'})
$targets=@($selected | Where-Object {$ids.Contains([UInt32]$_.ProcessId) -or ($Snapshot -and $_.CommandLine -and $_.CommandLine.Contains($Snapshot))})
$receipts=@($tracked)
foreach($taskProcess in $targets){
    $created=$taskProcess.CreationDate.ToString('o')
    if(!($receipts | Where-Object {$_.ProcessId -eq $taskProcess.ProcessId -and $_.CreationDate -eq $created})){
        $receipts+=@([pscustomobject]@{ProcessId=$taskProcess.ProcessId;CreationDate=$created;ParentProcessId=$taskProcess.ParentProcessId;Name=$taskProcess.Name;CommandLine=$taskProcess.CommandLine})
    }
}
$os=Get-CimInstance Win32_OperatingSystem
$record=[pscustomobject]@{sampled=(Get-Date).ToString('o');query_success=$true;target_count=$targets.Count;worker_pid=$TargetWorker;snapshot=$Snapshot;
    tracking_scope=$(if($TargetWorker){'worker_snapshot_descendants'}else{'resources_only'});global_heavy_count=$heavy.Count;receipts=$receipts;
    processes=@($selected|Select-Object Name,ProcessId,ParentProcessId,@{Name='CreationDate';Expression={$_.CreationDate.ToString('o')}},CommandLine,UserModeTime,KernelModeTime,WorkingSetSize,PrivatePageCount);
    free_physical_kib=$os.FreePhysicalMemory;free_virtual_kib=$os.FreeVirtualMemory;total_virtual_kib=$os.TotalVirtualMemorySize;boot=$os.LastBootUpTime}
$json=$record|ConvertTo-Json -Depth 6
if($OutputPath){if(Test-Path -LiteralPath $OutputPath){throw 'Existing process sample preserved'};$json|Set-Content -LiteralPath $OutputPath -Encoding UTF8}
$json
