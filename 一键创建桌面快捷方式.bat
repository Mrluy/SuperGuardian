@echo off
chcp 65001 >nul
title 创建桌面快捷方式

set "EXE_NAME=SuperGuardian.exe"
set "SHORTCUT_NAME=超级守护"
set "SCRIPT_DIR=%~dp0"
set "EXE_PATH=%SCRIPT_DIR%%EXE_NAME%"

if not exist "%EXE_PATH%" (
    echo 错误：未找到 %EXE_NAME%
    echo 请将此脚本放在 %EXE_NAME% 同一目录下。
    pause
    exit /b 1
)

powershell -NoProfile -Command "$ws = New-Object -ComObject WScript.Shell; $sc = $ws.CreateShortcut([IO.Path]::Combine($ws.SpecialFolders('Desktop'), '%SHORTCUT_NAME%.lnk')); $sc.TargetPath = '%EXE_PATH%'; $sc.WorkingDirectory = '%SCRIPT_DIR%'; $sc.IconLocation = '%EXE_PATH%,0'; $sc.Save()"

if %errorlevel% equ 0 (
    echo 桌面快捷方式已创建：%SHORTCUT_NAME%
) else (
    echo 创建快捷方式失败，请检查权限。
)
pause