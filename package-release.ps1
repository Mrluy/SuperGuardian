<#
.SYNOPSIS
    SuperGuardian Release 打包脚本
.DESCRIPTION
    仅打包 x64\Release 中已编译好的程序，不执行编译。
    产物会先复制到 .\package\ 下的暂存目录，再压缩为 ZIP。
.PARAMETER Version
    可选版本号字符串（如 "1.0.0"），用于 ZIP 文件命名。
    不填则自动使用当前日期时间。
.PARAMETER NoZip
    仅生成暂存目录，不压缩为 ZIP。
.PARAMETER SkipQtDeploy
    跳过 windeployqt 部署步骤，适用于静态 Qt 或已手动处理运行时依赖的场景。
.EXAMPLE
    .\package-release.ps1
    .\package-release.ps1 -Version 1.2.0
    .\package-release.ps1 -NoZip
    .\package-release.ps1 -SkipQtDeploy
#>

[CmdletBinding()]
param(
    [string]$Version = '',
    [switch]$NoZip,
    [switch]$SkipQtDeploy
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root        = $PSScriptRoot
$platform    = 'x64'
$config      = 'Release'
$exeName     = 'SuperGuardian.exe'
$buildDir    = Join-Path $root "$platform\$config"
$exePath     = Join-Path $buildDir $exeName
$packageDir  = Join-Path $root 'package'
$readmePath  = Join-Path $root 'README.md'

function Write-Step([string]$Message) {
    Write-Host "  >> $Message" -ForegroundColor Cyan
}

function Write-OK([string]$Message) {
    Write-Host "  OK $Message" -ForegroundColor Green
}

function Find-WinDeployQt {
    $cmd = Get-Command windeployqt -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $searchRoots = @('C:\Qt', 'D:\Qt', 'E:\Qt', "$env:USERPROFILE\Qt")
    foreach ($r in $searchRoots) {
        if (-not (Test-Path $r)) { continue }
        $found = Get-ChildItem -Path $r -Recurse -Filter 'windeployqt.exe' -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    return $null
}

if (-not (Test-Path $exePath)) {
    throw "未找到 Release 输出文件，请先在 x64\Release 下完成编译: $exePath"
}

if ($Version -ne '') {
    $zipBaseName = "SuperGuardian_v$($Version.TrimStart('vV'))"
} else {
    $zipBaseName = "SuperGuardian_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
}

if (-not (Test-Path $packageDir)) {
    New-Item -Path $packageDir -ItemType Directory | Out-Null
}

$stagingDir = Join-Path $packageDir $zipBaseName
if (Test-Path $stagingDir) {
    Remove-Item $stagingDir -Recurse -Force
}
New-Item -Path $stagingDir -ItemType Directory | Out-Null

Write-Step '复制 Release 产物到暂存目录 ...'
Copy-Item $exePath (Join-Path $stagingDir $exeName) -Force
if (Test-Path $readmePath) {
    Copy-Item $readmePath (Join-Path $stagingDir 'README.md') -Force
}

if (-not $SkipQtDeploy) {
    $wdq = Find-WinDeployQt
    if ($wdq) {
        Write-Step '部署 Qt 运行时 ...'
        & $wdq --release --compiler-runtime --force --dir $stagingDir (Join-Path $stagingDir $exeName)
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "windeployqt 返回 ExitCode = $LASTEXITCODE；若这是静态 Qt 构建，可忽略此警告并继续打包。"
        }
    } else {
        Write-Warning '未找到 windeployqt，若程序依赖 Qt 运行时，请手动补充相关 DLL。'
    }
} else {
    Write-Step '已跳过 Qt 运行时部署（-SkipQtDeploy）'
}

if ($NoZip) {
    Write-Host ''
    Write-OK '暂存完成（未压缩）'
    Write-Host "     $stagingDir" -ForegroundColor Green
    return
}

$zipPath = Join-Path $packageDir "$zipBaseName.zip"
if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}

Write-Step '压缩打包 ...'
Compress-Archive -Path "$stagingDir\*" -DestinationPath $zipPath -CompressionLevel Optimal
Remove-Item $stagingDir -Recurse -Force

$sizeMb = [math]::Round((Get-Item $zipPath).Length / 1MB, 2)
Write-Host ''
Write-OK "Release 打包完成（$sizeMb MB）"
Write-Host "     $zipPath" -ForegroundColor Green
