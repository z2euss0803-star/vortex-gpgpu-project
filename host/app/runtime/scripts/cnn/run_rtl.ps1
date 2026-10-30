param([Parameter(Mandatory=$true)][string]$ManifestPath,[Parameter(Mandatory=$true)][string]$RunId)
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
& 'D:/PYTHON/python.exe' (Join-Path $taskRoot 'validation/h0/prepare.py') --run-id $RunId --manifest $ManifestPath
if($LASTEXITCODE -ne 0){throw 'CNN RTL preparation failed'}
& 'D:/PYTHON/python.exe' (Join-Path $taskRoot 'validation/h0/run.py') --workdir (Join-Path $taskRoot "build/$RunId/rtl")
if($LASTEXITCODE -ne 0){throw 'CNN RTL failed; preserve the run directory'}
