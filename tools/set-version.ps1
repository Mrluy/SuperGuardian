<#
.SYNOPSIS
    一键修改 SuperGuardian 项目中所有版本号。
.DESCRIPTION
    自动替换以下文件中的版本号：
      - src/app/main.cpp              (QCoreApplication::setApplicationVersion)
      - resources/app.rc              (FILEVERSION, PRODUCTVERSION, FileVersion, ProductVersion)
.PARAMETER Version
    版本号字符串，格式为 "X.Y.Z"（如 "1.0.7"）。
.EXAMPLE
    .\tools\set-version.ps1 -Version 1.0.7
    .\tools\set-version.ps1 1.2.0
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

# 解析版本号组件
$parts = $Version.Split('.')
$major = $parts[0]
$minor = $parts[1]
$patch = $parts[2]
$commaVer = "$major,$minor,$patch,0"     # 1,0,7,0
$dotVer   = "$major.$minor.$patch.0"     # 1.0.7.0

function Write-Change([string]$File, [string]$Detail) {
    Write-Host "  [OK] $File - $Detail" -ForegroundColor Green
}

function Write-Skip([string]$File, [string]$Detail) {
    Write-Host "  [--] $File - $Detail (already up to date)" -ForegroundColor DarkGray
}

$changedCount = 0

# ---- 1. src/app/main.cpp ----
$mainCpp = Join-Path $root 'src\app\main.cpp'
if (Test-Path $mainCpp) {
    $content = Get-Content $mainCpp -Raw -Encoding UTF8
    $pattern = 'setApplicationVersion\(u"[^"]*"_s\)'
    $replacement = "setApplicationVersion(u`"$Version`"_s)"
    if ($content -match $pattern -and $Matches[0] -ne $replacement) {
        $content = $content -replace $pattern, $replacement
        [System.IO.File]::WriteAllText($mainCpp, $content, [System.Text.UTF8Encoding]::new($false))
        Write-Change 'src/app/main.cpp' "setApplicationVersion -> $Version"
        $changedCount++
    } else {
        Write-Skip 'src/app/main.cpp' $Version
    }
} else {
    Write-Host "  [!!] src/app/main.cpp not found" -ForegroundColor Red
}

# ---- 2. resources/app.rc ----
$rcFile = Join-Path $root 'resources\app.rc'
if (Test-Path $rcFile) {
    $content = Get-Content $rcFile -Raw -Encoding UTF8
    $changed = $false

    # FILEVERSION x,y,z,0
    $fvPattern = '(?<=FILEVERSION\s+)\d+,\d+,\d+,\d+'
    if ($content -match $fvPattern -and $Matches[0] -ne $commaVer) {
        $content = $content -replace $fvPattern, $commaVer
        $changed = $true
    }

    # PRODUCTVERSION x,y,z,0
    $pvPattern = '(?<=PRODUCTVERSION\s+)\d+,\d+,\d+,\d+'
    if ($content -match $pvPattern -and $Matches[0] -ne $commaVer) {
        $content = $content -replace $pvPattern, $commaVer
        $changed = $true
    }

    # VALUE "FileVersion", "x.y.z.0"
    $fvStrPattern = '(?<=VALUE\s+"FileVersion",\s*")\d+\.\d+\.\d+\.\d+(?=")'
    if ($content -match $fvStrPattern -and $Matches[0] -ne $dotVer) {
        $content = $content -replace $fvStrPattern, $dotVer
        $changed = $true
    }

    # VALUE "ProductVersion", "x.y.z.0"
    $pvStrPattern = '(?<=VALUE\s+"ProductVersion",\s*")\d+\.\d+\.\d+\.\d+(?=")'
    if ($content -match $pvStrPattern -and $Matches[0] -ne $dotVer) {
        $content = $content -replace $pvStrPattern, $dotVer
        $changed = $true
    }

    if ($changed) {
        [System.IO.File]::WriteAllText($rcFile, $content, [System.Text.UTF8Encoding]::new($false))
        Write-Change 'resources/app.rc' "FILEVERSION/PRODUCTVERSION -> $commaVer, strings -> $dotVer"
        $changedCount++
    } else {
        Write-Skip 'resources/app.rc' $dotVer
    }
} else {
    Write-Host "  [!!] resources/app.rc not found" -ForegroundColor Red
}

Write-Host ""
if ($changedCount -gt 0) {
    Write-Host "Done! Updated $changedCount file(s) to version $Version." -ForegroundColor Cyan
} else {
    Write-Host "All files already at version $Version. No changes made." -ForegroundColor Yellow
}
