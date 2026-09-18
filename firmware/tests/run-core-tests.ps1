[CmdletBinding()]
param([string]$VisualStudioPath)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($VisualStudioPath)) {
    $locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $locator)) {
        throw 'Visual Studio C/C++ tools are required, or pass -VisualStudioPath.'
    }
    $VisualStudioPath = & $locator -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if ([string]::IsNullOrWhiteSpace($VisualStudioPath)) { throw 'Visual Studio C/C++ tools were not found.' }
Import-Module (Join-Path $VisualStudioPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $VisualStudioPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

$firmwareDir = Split-Path -Parent $PSScriptRoot
$coreDir = Join-Path $firmwareDir 'iwatch\src\core'
$outputDir = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
Push-Location $outputDir
try {
    $compileArgs = @('/nologo', '/std:c11', '/utf-8', '/W4', '/WX', '/Od', '/Z7',
        "/I$coreDir", '/Fetest_input_queue.exe',
        (Join-Path $PSScriptRoot 'test_input_queue.c'), (Join-Path $coreDir 'iw_input_queue.c'))
    & cl.exe @compileArgs
    if ($LASTEXITCODE -ne 0) { throw 'Host input test compilation failed.' }
    & (Join-Path $outputDir 'test_input_queue.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Host input tests failed.' }
    & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /Od /Z7 "/I$coreDir" /Fetest_keys.exe (Join-Path $PSScriptRoot 'test_keys.c') (Join-Path $coreDir 'iw_keys.c')
    if ($LASTEXITCODE -ne 0) { throw 'D09 双键状态机测试编译失败。' }
    & ./test_keys.exe
    if ($LASTEXITCODE -ne 0) { throw 'D09 双键状态机测试失败。' }
    & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /Od /Z7 "/I$coreDir" /Fetest_gui_wait.exe (Join-Path $PSScriptRoot 'test_gui_wait.c') (Join-Path $coreDir 'iw_gui_wait.c') (Join-Path $coreDir 'iw_input_queue.c') (Join-Path $coreDir 'iw_display_guard.c')
    if ($LASTEXITCODE -ne 0) { throw 'GUI 等待测试编译失败。' }
    & ./test_gui_wait.exe
    if ($LASTEXITCODE -ne 0) { throw 'GUI 等待与恢复测试失败。' }
    $platformDir = Join-Path $firmwareDir 'iwatch/src/platform'
    $serviceDir = Join-Path $firmwareDir 'iwatch/src/services'
    & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /Od /Z7 "/I$PSScriptRoot/key_port_mocks" "/I$coreDir" "/I$platformDir" /Fetest_key_port.exe (Join-Path $PSScriptRoot 'test_key_port.c') (Join-Path $coreDir 'iw_keys.c') (Join-Path $coreDir 'iw_input_queue.c')
    if ($LASTEXITCODE -ne 0) { throw 'D09 实体键适配测试编译失败。' }
    & ./test_key_port.exe
    if ($LASTEXITCODE -ne 0) { throw 'D09 实体键适配测试失败。' }

    & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /Od "/I$coreDir" "/I$platformDir" "/I$PSScriptRoot/mocks" /Fetest_gui_port.exe (Join-Path $PSScriptRoot 'test_gui_port.c') (Join-Path $platformDir 'iw_gui_port.c') (Join-Path $coreDir 'iw_gui_wait.c')
    if ($LASTEXITCODE -ne 0) { throw 'GUI 事件适配测试编译失败。' }
    & ./test_gui_port.exe
    if ($LASTEXITCODE -ne 0) { throw 'GUI 事件适配测试失败。' }
    & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /Od /Z7 "/I$platformDir" "/I$serviceDir" "/I$coreDir" /Fetest_display.exe (Join-Path $PSScriptRoot 'test_display.c') (Join-Path $platformDir 'iw_display.c')
    if ($LASTEXITCODE -ne 0) { throw '显示目标邮箱测试编译失败。' }
    & ./test_display.exe
    if ($LASTEXITCODE -ne 0) { throw '显示目标邮箱测试失败。' }
    & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /Od /Z7 "/I$coreDir" /Fetest_time.exe (Join-Path $PSScriptRoot 'test_time.c') (Join-Path $coreDir 'iw_time.c')
    if ($LASTEXITCODE -ne 0) { throw '时间核心测试编译失败。' }
    & ./test_time.exe
    if ($LASTEXITCODE -ne 0) { throw '时间核心测试失败。' }
    & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /Od /Z7 "/I$coreDir" /Fetest_chronograph.exe (Join-Path $PSScriptRoot 'test_chronograph.c') (Join-Path $coreDir 'iw_chronograph.c')
    if ($LASTEXITCODE -ne 0) { throw 'D11 计时器与秒表核心测试编译失败。' }
    & ./test_chronograph.exe
    if ($LASTEXITCODE -ne 0) { throw 'D11 T16/T17 核心测试失败。' }
    & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /Od /Z7 "/I$platformDir" "/I$PSScriptRoot/mocks" /Fetest_time_rtc.exe (Join-Path $PSScriptRoot 'test_time_rtc.c') (Join-Path $platformDir 'iw_time_rtc.c')
    if ($LASTEXITCODE -ne 0) { throw 'RTC 可信标记与会话测试编译失败。' }
    & ./test_time_rtc.exe
    if ($LASTEXITCODE -ne 0) { throw 'RTC 可信标记与会话测试失败。' }
    & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /Od /Z7 "/I$coreDir" "/I$serviceDir" /Fetest_service.exe (Join-Path $PSScriptRoot 'test_service.c') (Join-Path $serviceDir 'iw_service.c') (Join-Path $coreDir 'iw_time.c') (Join-Path $coreDir 'iw_chronograph.c')
    if ($LASTEXITCODE -ne 0) { throw '服务账本测试编译失败。' }
    & ./test_service.exe
    if ($LASTEXITCODE -ne 0) { throw '服务账本测试失败。' }
    & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /Od /Z7 "/I$PSScriptRoot/mocks" "/I$platformDir" "/I$serviceDir" "/I$coreDir" /Fetest_boot.exe (Join-Path $PSScriptRoot 'test_boot.c') (Join-Path $platformDir 'iw_boot.c')
    if ($LASTEXITCODE -ne 0) { throw '启动协调失败注入测试编译失败。' }
    & ./test_boot.exe
    if ($LASTEXITCODE -ne 0) { throw '启动协调失败注入测试失败。' }
    $guiCoreDir = Join-Path $firmwareDir 'iwatch/src/gui_core'
    & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /Od /Z7 "/I$guiCoreDir" "/I$coreDir" "/I$serviceDir" /Fetest_ui_commands.exe (Join-Path $PSScriptRoot 'test_ui_commands.c') (Join-Path $guiCoreDir 'iw_ui_commands.c') (Join-Path $serviceDir 'iw_service.c') (Join-Path $coreDir 'iw_time.c') (Join-Path $coreDir 'iw_chronograph.c')
    if ($LASTEXITCODE -ne 0) { throw 'D10 全局结果客户端编译失败。' }
    & ./test_ui_commands.exe
    if ($LASTEXITCODE -ne 0) { throw 'D10 全局结果客户端测试失败。' }
    & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /Od /Z7 "/I$guiCoreDir" "/I$coreDir" "/I$serviceDir" /Fetest_settings_model.exe (Join-Path $PSScriptRoot 'test_settings_model.c') (Join-Path $guiCoreDir 'iw_settings_model.c') (Join-Path $coreDir 'iw_time.c')
    if ($LASTEXITCODE -ne 0) { throw 'D10 设置数据模型编译失败。' }
    & ./test_settings_model.exe
    if ($LASTEXITCODE -ne 0) { throw 'D10 日历、草稿及亮度边界测试失败。' }
    & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /Od /Z7 "/I$guiCoreDir" /Fetest_navigation.exe (Join-Path $PSScriptRoot 'test_navigation.c') (Join-Path $guiCoreDir 'iw_scope.c') (Join-Path $guiCoreDir 'iw_routes.c') (Join-Path $guiCoreDir 'iw_navigator.c')
    if ($LASTEXITCODE -ne 0) { throw 'D08 作用域与导航测试编译失败。' }
    & ./test_navigation.exe
    if ($LASTEXITCODE -ne 0) { throw 'D08 作用域与导航测试失败。' }
    & py -3 (Join-Path $PSScriptRoot 'generate_lifecycle_test.py')
    if ($LASTEXITCODE -ne 0) { throw '生命周期测试生成失败。' }
    foreach ($page in @('clock', 'menu', 'status', 'simple', 'dial', 'rotate_bg')) {
        # 替身中的空回调允许未使用参数；AddressSanitizer 检查实际生产回收函数。
        & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /wd4505 /wd4100 /wd4189 /D_CRT_SECURE_NO_WARNINGS /fsanitize=address /Zi "/Fetest_${page}_lifecycle.exe" "test_${page}_lifecycle.c"
        if ($LASTEXITCODE -ne 0) { throw "$page 生命周期测试编译失败。" }
        & "./test_${page}_lifecycle.exe"
        if ($LASTEXITCODE -ne 0) { throw "$page 生命周期测试失败。" }
    }
    & py -3 -m unittest discover -s $PSScriptRoot -p 'test_*.py' -v
    if ($LASTEXITCODE -ne 0) { throw 'Layout regression tests failed.' }
    & (Join-Path $PSScriptRoot 'test-build-lock.ps1')
    & (Join-Path $PSScriptRoot 'test-powershell-syntax.ps1')
}
finally { Pop-Location }
