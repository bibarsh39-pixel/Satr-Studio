param([string]$EvidenceDirectory='evidence/0.9.0')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$env:PATH='C:\Windows\System32;C:\Windows'
$env:PYTHONHOME='';$env:PYTHONPATH='';$env:NODE_PATH=''
Remove-Item Env:SATR_TEST_SCREENSHOTS -ErrorAction SilentlyContinue
foreach($mode in @('language','smoke')){
    $directory=Join-Path $root ($EvidenceDirectory+"/gui-"+$mode)
    New-Item -ItemType Directory -Force -Path "$directory/fixtures" | Out-Null
    Get-ChildItem -LiteralPath "$root/tests/four_languages" -File | Copy-Item -Destination "$directory/fixtures"
    if(Test-Path -LiteralPath "$directory/gui-smoke.json"){Remove-Item -LiteralPath "$directory/gui-smoke.json"}
    $p=Start-Process -FilePath "$root/dist/windows/Satr.exe" -ArgumentList @("--$mode-test",('"'+$directory+'"'),'--portable-test') -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(240000)){throw "Checks timed out; test process: $($p.Id)"}
    $result=Get-Content -LiteralPath "$directory/gui-smoke.json" -Raw -Encoding UTF8 | ConvertFrom-Json
    $result.checks | Where-Object {!$_.passed} | ConvertTo-Json -Depth 6
    Write-Output "$mode : $($result.checks.Count) checks, passed: $($result.passed)"
    if(!$result.passed){exit 1}
}

