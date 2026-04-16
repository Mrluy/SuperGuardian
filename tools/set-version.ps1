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

function Write-Utf8NoBom([string]$Path, [string]$Content) {
    [System.IO.File]::WriteAllText($Path, $Content, [System.Text.UTF8Encoding]::new($false))
}

function Update-ByRegex(
    [string]$Path,
    [string]$Pattern,
    [string]$Replacement,
    [string]$ChangedMessage,
    [string]$SkipMessage
) {
    if (-not (Test-Path $Path)) {
        return $false
    }

    $content = Get-Content $Path -Raw -Encoding UTF8
    $updated = [regex]::Replace($content, $Pattern, $Replacement, 1)
    if ($updated -ne $content) {
        Write-Utf8NoBom $Path $updated
        Write-Change ($Path.Substring($root.Length + 1).Replace('/', '\')) $ChangedMessage
        return $true
    }

    Write-Skip ($Path.Substring($root.Length + 1).Replace('/', '\')) $SkipMessage
    return $false
}

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
    $pattern = 'QCoreApplication::setApplicationVersion\s*\(\s*u"[^"]*"_s\s*\)'
    $replacement = "QCoreApplication::setApplicationVersion(u`"$Version`"_s)"
    if (Update-ByRegex $mainCpp $pattern $replacement "setApplicationVersion -> $Version" $Version) {
        $changedCount++
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
        Write-Utf8NoBom $rcFile $content
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
