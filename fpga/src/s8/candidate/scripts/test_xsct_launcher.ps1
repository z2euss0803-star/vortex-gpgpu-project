# Run with Windows PowerShell 5, matching the actual pipeline's native handling.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root
$python='C:\Users\13151\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$env:TEMP=Join-Path $root 'build\tmp'; $env:TMP=$env:TEMP
$results=@()
foreach($expected in @(0,1)) {
  $log="xsct_launcher_probe_$expected.log"
  & $python scripts/run_xsct_job.py probe_xsct_launcher.tcl $log $expected
  $actual=$LASTEXITCODE
  if($actual -ne $expected){throw "XSCT exit code mismatch: expected=$expected actual=$actual"}
  $raw=Get-Content -LiteralPath (Join-Path $root "evidence\$log") -Raw
  if(!$raw.Contains('XSCT_PROBE_STDERR') -or !$raw.Contains("XSCT_PROBE_STDOUT exit=$expected")) {
    throw 'XSCT probe lost stdout or stderr'
  }
  $results += [pscustomobject]@{expected=$expected;actual=$actual;stdout=$true;stderr=$true}
}
[pscustomobject]@{pass=$true;version=$PSVersionTable.PSVersion.ToString();results=$results;checked=(Get-Date -Format o)} |
  ConvertTo-Json -Depth 5 | Set-Content -LiteralPath evidence/xsct_launcher_probe.json
Write-Output 'XSCT_FILE_LAUNCHER_PROBE_PASS'
