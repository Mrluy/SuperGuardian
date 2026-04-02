<#
.SYNOPSIS
    SuperGuardian 打包脚本
.DESCRIPTION
    编译项目并将产物压缩打包至 .\package\ 文件夹。
.PARAMETER Configuration
    编译配置：Release（默认）或 Debug。
.PARAMETER Platform
    目标平台，默认 x64。
.PARAMETER Version
    可选版本号字符串（如 "1.0.0"），用于 ZIP 文件命名。
    不填则自动使用当前日期时间。
.PARAMETER SkipBuild
    跳过编译步骤，直接打包已有产物。
.PARAMETER NoZip
    仅暂存文件，不生成 ZIP（用于调试打包内容）。
.EXAMPLE
    .\package.ps1
    .\package.ps1 -Version 1.2.0
    .\package.ps1 -SkipBuild -Version 1.2.0
    .\package.ps1 -Configuration Debug
#>

[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',

    [string]$Platform = 'x64',

    [string]$Version = '',

    [switch]$SkipBuild,

    [switch]$NoZip
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# 路径常量
$root        = $PSScriptRoot
$exeName     = 'SuperGuardian.exe'
$buildDir    = Join-Path $root "$Platform\$Configuration"
$exePath     = Join-Path $buildDir $exeName
$packageDir  = Join-Path $root 'package'
$projectFile = Join-Path $root 'SuperGuardian.vcxproj'

# 辅助函数
function Write-Step([string]$Message) {
    Write-Host "  >> $Message" -ForegroundColor Cyan
}

function Write-OK([string]$Message) {
    Write-Host "  OK $Message" -ForegroundColor Green
}

function Find-MSBuild {
    $cmd = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -requires Microsoft.Component.MSBuild `
                            -find 'MSBuild\**\Bin\MSBuild.exe' 2>$null |
                     Select-Object -First 1
        if ($found) { return $found }
    }

    throw '未找到 MSBuild，请确认已安装 Visual Studio C++ 生成工具。'
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

# Step 1：编译
if ($SkipBuild) {
    Write-Step "已跳过编译步骤（-SkipBuild）"
} else {
    Write-Step "编译 $Configuration|$Platform ..."
    if (-not (Test-Path $projectFile)) {
        throw "未找到项目文件: $projectFile"
    }
    $msbuild = Find-MSBuild
    Write-Host "     MSBuild: $msbuild" -ForegroundColor DarkGray
    & $msbuild $projectFile /m /nologo /t:Build `
        "/p:Configuration=$Configuration;Platform=$Platform"
    if ($LASTEXITCODE -ne 0) {
        throw "编译失败（MSBuild ExitCode = $LASTEXITCODE）"
    }
    Write-OK "编译完成"
}

if (-not (Test-Path $exePath)) {
    throw "未找到输出文件，请先编译: $exePath"
}

# Step 2：确定 ZIP 名称
$dateStr = Get-Date -Format 'yyyyMMdd_HHmmss'
if ($Version -ne '') {
    $zipBaseName = "SuperGuardian_v$($Version.TrimStart('vV'))_$dateStr"
} else {
    $zipBaseName = "SuperGuardian_$dateStr"
}

# Step 3：准备暂存目录
if (-not (Test-Path $packageDir)) {
    New-Item -Path $packageDir -ItemType Directory | Out-Null
}

$stagingDir = Join-Path $packageDir $zipBaseName
if (Test-Path $stagingDir) {
    Remove-Item $stagingDir -Recurse -Force
}
New-Item -Path $stagingDir -ItemType Directory | Out-Null

# Step 4：复制产物
Write-Step "复制文件至暂存目录 ..."

# 主程序（Release 为静态单文件，Debug 可能需要 Qt DLL）
Copy-Item $exePath (Join-Path $stagingDir $exeName) -Force
Write-Host "     $exeName  ($([math]::Round((Get-Item $exePath).Length / 1MB, 2)) MB)" -ForegroundColor DarkGray

# README（存在则附带）
$readmePath = Join-Path $root 'README.md'
if (Test-Path $readmePath) {
    Copy-Item $readmePath (Join-Path $stagingDir 'README.md') -Force
    Write-Host "     README.md" -ForegroundColor DarkGray
}

# Debug 模式：部署 Qt 运行时
if ($Configuration -eq 'Debug') {
    Write-Step "Debug 模式：部署 Qt 运行时 ..."
    $wdq = Find-WinDeployQt
    if ($wdq) {
        Write-Host "     windeployqt: $wdq" -ForegroundColor DarkGray
        & $wdq --debug --force --compiler-runtime `
               --dir $stagingDir (Join-Path $stagingDir $exeName)
        if ($LASTEXITCODE -ne 0) {
            throw "windeployqt 执行失败（ExitCode = $LASTEXITCODE）"
        }
    } else {
        Write-Warning "未找到 windeployqt，Debug 包可能缺少 Qt 运行时 DLL。"
    }
}

# Step 5：压缩
if ($NoZip) {
    Write-Host ''
    Write-OK "暂存完成（未压缩）"
    Write-Host "     $stagingDir" -ForegroundColor Green
} else {
    $zipPath = Join-Path $packageDir "$zipBaseName.zip"
    if (Test-Path $zipPath) {
        Remove-Item $zipPath -Force
    }

    Write-Step "压缩打包 ..."
    Compress-Archive -Path "$stagingDir\*" -DestinationPath $zipPath -CompressionLevel Optimal

    # 清理暂存目录
    Remove-Item $stagingDir -Recurse -Force

    $sizeMb = [math]::Round((Get-Item $zipPath).Length / 1MB, 2)
    Write-Host ''
    Write-OK "打包完成（$sizeMb MB）"
    Write-Host "     $zipPath" -ForegroundColor Green
}
