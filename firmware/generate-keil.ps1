[CmdletBinding()]
param(
    [string]$SdkPath = $env:SIFLI_SDK_PATH,
    [string]$KeilPath,
    [ValidateRange(1, 64)]
    [int]$Jobs = 8
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($SdkPath)) {
    $SdkPath = 'C:\OpenSiFli\SiFli-SDK-v2.5.1'
}
$SdkPath = [System.IO.Path]::GetFullPath($SdkPath)
$exportScript = Join-Path $SdkPath 'export.ps1'
if (-not (Test-Path -LiteralPath $exportScript)) {
    throw "SiFli SDK export script was not found: $exportScript"
}

if ([string]::IsNullOrWhiteSpace($KeilPath)) {
    $mdk = Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Keil\Products\MDK' -ErrorAction SilentlyContinue
    if ($mdk -and $mdk.Path) {
        $KeilPath = Split-Path -Parent $mdk.Path
    }
}
if ([string]::IsNullOrWhiteSpace($KeilPath)) {
    throw 'Keil MDK was not found. Pass its installation root with -KeilPath.'
}
$KeilPath = [System.IO.Path]::GetFullPath($KeilPath)
if (-not (Test-Path -LiteralPath (Join-Path $KeilPath 'ARM\ARMCLANG\bin\armclang.exe'))) {
    throw "Arm Compiler 6 was not found under: $KeilPath"
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'build-common.ps1')
$buildRoot = Get-IwatchBuildRoot -RepositoryRoot $repositoryRoot -SdkPath $SdkPath
$firmwareRoot = Join-Path $buildRoot 'firmware'
$projectDir = Join-Path $firmwareRoot 'iwatch\project'
$boardDir = Join-Path $firmwareRoot 'boards'
$repositoryProjectDir = Join-Path $PSScriptRoot 'iwatch\project'

# Activate the SDK Python/SCons environment, then select the installed Keil toolchain.
. $exportScript -t gcc
if ($LASTEXITCODE -ne 0) {
    throw "SiFli SDK environment activation failed with exit code $LASTEXITCODE"
}
$env:RTT_CC = 'keil'
$env:RTT_EXEC_PATH = $KeilPath

Push-Location $projectDir
try {
    & python (Join-Path $firmwareRoot 'check_layout.py')
    if ($LASTEXITCODE -ne 0) { throw 'Memory layout validation failed.' }
    & scons '--board=iwatch_sf32lb58_a128_qspi' "--board_search_path=$boardDir" '--target=mdk5' "-j$Jobs"
    if ($LASTEXITCODE -ne 0) {
        throw "Keil project generation failed with exit code $LASTEXITCODE"
    }
    $keilProject = Join-Path $projectDir 'project.uvprojx'
    $projectText = [System.IO.File]::ReadAllText($keilProject)
    $projectText = $projectText.Replace('<TargetName>rt-thread</TargetName>', '<TargetName>iwatch</TargetName>')

    # The SDK's MDK generator omits the Custom Datapath Extension enabled by
    # SF32LB58.  Without it, arm_cde.h rejects every CP1 intrinsic when the
    # generated project is built from uVision.  Keep the setting in the
    # generator so regenerating project.uvprojx remains safe.
    $defaultCompilerOptions = '<MiscControls>-Wno-builtin-macro-redefined </MiscControls>'
    $sf32lb58CompilerOptions = '<MiscControls>-Wno-builtin-macro-redefined -march=armv8-m.main+cdecp1</MiscControls>'
    if (-not $projectText.Contains($defaultCompilerOptions)) {
        throw 'Generated Keil project does not contain the expected compiler options block.'
    }
    $projectText = $projectText.Replace($defaultCompilerOptions, $sf32lb58CompilerOptions)

    # The SDK cannot generate a project from a path containing Chinese characters, so SCons
    # runs through the ASCII junction. Paths to the external SDK are therefore relative to
    # that junction and would be invalid when this project is opened from the repository.
    # Write the selected SDK location into the generated project so all references resolve.
    $relativeSdkPath = [System.IO.Path]::GetRelativePath($projectDir, $SdkPath)
    $escapedSdkPath = [System.Security.SecurityElement]::Escape($SdkPath)
    $projectText = $projectText.Replace($relativeSdkPath, $escapedSdkPath)
    [System.IO.File]::WriteAllText($keilProject, $projectText, [System.Text.UTF8Encoding]::new($false))

    [xml]$projectXml = [System.IO.File]::ReadAllText((Join-Path $repositoryProjectDir 'project.uvprojx'))
    $missingFiles = [System.Collections.Generic.List[string]]::new()
    foreach ($fileNode in $projectXml.SelectNodes('//FilePath')) {
        $filePath = [Environment]::ExpandEnvironmentVariables($fileNode.InnerText)
        if ([System.IO.Path]::IsPathRooted($filePath)) {
            $absoluteFilePath = $filePath
        }
        else {
            $absoluteFilePath = [System.IO.Path]::GetFullPath((Join-Path $repositoryProjectDir $filePath))
        }
        if (-not (Test-Path -LiteralPath $absoluteFilePath)) {
            $missingFiles.Add($fileNode.InnerText)
        }
    }
    if ($missingFiles.Count -ne 0) {
        throw "Generated Keil project has $($missingFiles.Count) missing source references. First missing file: $($missingFiles[0])"
    }

    Write-Output "Keil project: $keilProject"
    Write-Output "Repository file: $(Join-Path $repositoryProjectDir 'project.uvprojx')"
    Write-Output "Validated source references: $($projectXml.SelectNodes('//FilePath').Count)"
}
finally {
    Pop-Location
}
