$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $root
$directory = 'work/v00/type-specimens'
New-Item -ItemType Directory -Force "$directory/raw-probe" | Out-Null
$vs = 'E:\Program Files\Microsoft Visual Studio\2022\Community'
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$env:V00_SPEC_OUTPUT = "$directory/raw-probe"
$exe = 'firmware/tests/build/d07-a0/tiny-ttf-oom/test_tiny_ttf_oom.exe'
$results = @()
foreach ($line in (Get-Content "$directory/candidate-size.log" -Encoding UTF8)) {
    if ($line -notmatch 'role=(\w+) index=(\d+) text=.* size=(\d+) tracking=') { continue }
    $role = $Matches[1]
    $index = [int]$Matches[2]
    $env:V00_SPEC_SIZE = $Matches[3]
    $weight = if ($role -eq 'ALARM_SEPARATOR') { 300 }
        elseif ($role -in @('FACE_TIME', 'ALARM_VALUE', 'ROW_TITLE', 'ROW_DETAIL')) { 400 }
        elseif ($role -in @('CONTROL_BATTERY', 'ACTION_LABEL', 'PAGE_TITLE')) { 500 }
        else { 600 }
    $font = "$directory/subsets/NotoSansSC-v00-only-$weight.ttf"
    $output = & $exe $font component_v00_font_specimen $index 2>&1
    if ($LASTEXITCODE -ne 0) { throw "子集字库样例 $index 失败：$output" }
    $name = '{0:d2}.rgb565' -f $index
    $fullHash = (Get-FileHash "$directory/raw-candidate/$name" -Algorithm SHA256).Hash
    $subsetHash = (Get-FileHash "$directory/raw-probe/$name" -Algorithm SHA256).Hash
    $match = $fullHash -eq $subsetHash
    $results += "$index $role $weight $(if ($match) { 'MATCH' } else { 'DIFF' })"
}
$results | Set-Content "$directory/subset-parity.log" -Encoding UTF8
Remove-Item Env:V00_SPEC_OUTPUT,Env:V00_SPEC_SIZE -ErrorAction SilentlyContinue
if ($results.Count -ne 35 -or @($results | Where-Object { $_ -match 'DIFF' }).Count -ne 0) {
    throw 'V00 子集字体与全量候选帧存在差异'
}
Write-Output "V00 FONT SUBSET PARITY OK: $($results.Count) RGB565 frames"
