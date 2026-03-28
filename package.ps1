param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [string]$Platform = "x64",

    [string]$ProjectFile = (Join-Path $PSScriptRoot "SuperGuardian.vcxproj"),

    [string]$OutputRoot = (Join-Path $PSScriptRoot "artifacts"),

    [string]$PackageName = "SuperGuardian",

    [switch]$SkipBuild,

    [switch]$SkipDeployQt,

    [switch]$NoZip
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Write-Step([string]$Message) {
    Write-Host "[PACK] $Message" -ForegroundColor Cyan
}

function Resolve-MSBuild {
    $msbuild = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($msbuild) { return $msbuild.Source }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $path = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
        if ($path) { return $path }
    }

    throw "未找到 MSBuild，请确保已安装 Visual Studio C++ 构建工具。"
}

function Resolve-WinDeployQt {
    $tool = Get-Command windeployqt -ErrorAction SilentlyContinue
    if ($tool) { return $tool.Source }

    $searchRoots = @(
        (Join-Path $env:USERPROFILE "Qt"),
        "C:\Qt",
        "D:\Qt",
        "E:\Qt",
        "$env:ProgramFiles\Qt",
        "$env:ProgramFiles(x86)\Qt"
    ) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -Unique

    foreach ($root in $searchRoots) {
        $candidate = Get-ChildItem -Path $root -Recurse -Filter windeployqt.exe -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) { return $candidate.FullName }
    }

    return $null
}

function Test-RuntimeFiles([string]$Dir) {
    $required = @(
        (Join-Path $Dir "platforms\qwindows.dll")
    )
    foreach ($f in $required) {
        if (-not (Test-Path $f)) { return $false }
    }

    $qtCore = Get-ChildItem -Path $Dir -Filter "Qt6Core*.dll" -ErrorAction SilentlyContinue | Select-Object -First 1
    return $null -ne $qtCore
}

if (-not (Test-Path $ProjectFile)) {
    throw "项目文件不存在: $ProjectFile"
}

$buildOutputDir = Join-Path $PSScriptRoot (Join-Path $Platform $Configuration)
$appExe = Join-Path $buildOutputDir "SuperGuardian.exe"
$watchdogExe = Join-Path $buildOutputDir "SuperGuardianWatchdog.exe"

if (-not $SkipBuild) {
    Write-Step "开始编译: Configuration=$Configuration Platform=$Platform"
    $msbuildPath = Resolve-MSBuild
    & $msbuildPath $ProjectFile /m /t:Build "/p:Configuration=$Configuration;Platform=$Platform"
    if ($LASTEXITCODE -ne 0) {
        throw "编译失败，MSBuild ExitCode=$LASTEXITCODE"
    }
}

if (-not (Test-Path $appExe)) {
    throw "未找到主程序: $appExe"
}

if (-not (Test-Path $watchdogExe)) {
    Write-Step "未找到 SuperGuardianWatchdog.exe，尝试继续（可能是后处理未执行）。"
}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$stagingDir = Join-Path $OutputRoot "$PackageName-$Configuration-$timestamp"

if (Test-Path $stagingDir) {
    Remove-Item -Path $stagingDir -Recurse -Force
}
New-Item -Path $stagingDir -ItemType Directory | Out-Null

Write-Step "复制可执行文件到暂存目录"
Copy-Item -Path $appExe -Destination (Join-Path $stagingDir "SuperGuardian.exe") -Force
if (Test-Path $watchdogExe) {
    Copy-Item -Path $watchdogExe -Destination (Join-Path $stagingDir "SuperGuardianWatchdog.exe") -Force
}

if (-not $SkipDeployQt) {
    $windeployqt = Resolve-WinDeployQt
    if ($windeployqt) {
        Write-Step "执行 windeployqt: $windeployqt"
        $qtArgs = @()
        if ($Configuration -eq "Release") {
            $qtArgs += "--release"
        }
        else {
            $qtArgs += "--debug"
        }
        $qtArgs += "--force"
        $qtArgs += "--compiler-runtime"
        $qtArgs += "--dir"
        $qtArgs += $stagingDir
        $qtArgs += (Join-Path $stagingDir "SuperGuardian.exe")
        & $windeployqt @qtArgs
        if ($LASTEXITCODE -ne 0) {
            throw "windeployqt 执行失败，ExitCode=$LASTEXITCODE"
        }
        if (-not (Test-RuntimeFiles -Dir $stagingDir)) {
            throw "Qt 运行时部署不完整：缺少 Qt 核心 DLL 或 platforms/qwindows.dll"
        }
    }
    else {
        throw "未找到 windeployqt。请先安装 Qt 并确保 windeployqt 可用，或显式使用 -SkipDeployQt。"
    }
}

if (-not $NoZip) {
    $zipPath = "$stagingDir.zip"
    if (Test-Path $zipPath) {
        Remove-Item -Path $zipPath -Force
    }

    Write-Step "生成压缩包: $zipPath"
    Compress-Archive -Path (Join-Path $stagingDir "*") -DestinationPath $zipPath -CompressionLevel Optimal
    Write-Step "打包完成（ZIP）: $zipPath"
}
else {
    Write-Step "打包完成（目录）: $stagingDir"
}
