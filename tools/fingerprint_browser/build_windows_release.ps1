# Copyright (c) 2026 The Brave Authors. All rights reserved.
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at https://mozilla.org/MPL/2.0/.

[CmdletBinding()]
param(
    [string]$DestinationDirectory,
    [string]$NodePath,
    [string]$PythonPath,
    [string]$VisualStudioPath,
    [ValidateRange(1, 128)]
    [int]$Jobs = 8,
    [ValidateRange(1, 10)]
    [int]$BuildAttempts = 3,
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-PeMachine {
    param([Parameter(Mandatory)][string]$Path)

    $stream = [System.IO.File]::OpenRead($Path)
    $reader = [System.IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) {
            throw "$Path is not a PE image."
        }
        $stream.Position = 0x3c
        $peOffset = $reader.ReadUInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "$Path has an invalid PE signature."
        }
        return $reader.ReadUInt16()
    } finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

if ([System.Environment]::OSVersion.Platform -ne
    [System.PlatformID]::Win32NT) {
    throw 'Fingerprint Browser Windows releases must be built on Windows.'
}

$braveRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$package = Get-Content -LiteralPath (Join-Path $braveRoot 'package.json') -Raw |
    ConvertFrom-Json
$braveVersion = [string]$package.version
$chromiumMilestone = ([string]$package.config.projects.chrome.tag).Split('.')[0]
$expectedProductVersion = "$chromiumMilestone.$braveVersion"

if (-not $NodePath) {
    $nodeCandidates = @(
        Get-ChildItem -LiteralPath (Join-Path $braveRoot '.devtools') `
            -Filter 'node.exe' -File -Recurse -Depth 2 `
            -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty FullName
    )
    $pathNode = Get-Command node -ErrorAction SilentlyContinue
    if ($pathNode) {
        $nodeCandidates += $pathNode.Source
    }
    foreach ($candidatePath in $nodeCandidates) {
        $candidateVersion = & $candidatePath -p 'process.versions.node'
        if ($LASTEXITCODE -eq 0 -and [int]$candidateVersion.Split('.')[0] -eq 24) {
            $NodePath = $candidatePath
            break
        }
    }
}
if (-not $NodePath -or -not (Test-Path -LiteralPath $NodePath -PathType Leaf)) {
    throw 'Node.js 24 is required to build Fingerprint Browser.'
}
$nodeVersion = & $NodePath -p 'process.versions.node'
if ($LASTEXITCODE -ne 0 -or [int]$nodeVersion.Split('.')[0] -ne 24) {
    throw "Node.js 24 is required; selected runtime is $nodeVersion."
}
$nodeDirectory = Split-Path -Parent (Resolve-Path $NodePath).Path
$env:Path = $nodeDirectory + [System.IO.Path]::PathSeparator + $env:Path

if (-not $PythonPath) {
    $pythonCandidates = @()
    if ($env:PYTHON) {
        $pythonCandidates += $env:PYTHON
    }
    foreach ($commandName in @('python', 'python3')) {
        $pythonCommand = Get-Command $commandName -ErrorAction SilentlyContinue
        if ($pythonCommand) {
            $pythonCandidates += $pythonCommand.Source
        }
    }
    foreach ($candidatePath in $pythonCandidates | Select-Object -Unique) {
        if (-not (Test-Path -LiteralPath $candidatePath -PathType Leaf)) {
            continue
        }
        $candidateVersion = & $candidatePath -c `
            'import sys; print(*sys.version_info[:3], sep=chr(46))' `
            2>$null
        if ($LASTEXITCODE -eq 0 -and
            [version]$candidateVersion -ge [version]'3.10') {
            $PythonPath = $candidatePath
            break
        }
    }
}
if (-not $PythonPath -or
    -not (Test-Path -LiteralPath $PythonPath -PathType Leaf)) {
    throw 'Python 3.10 or newer is required to build Fingerprint Browser.'
}
$pythonVersion = & $PythonPath -c `
    'import sys; print(*sys.version_info[:3], sep=chr(46))'
if ($LASTEXITCODE -ne 0 -or [version]$pythonVersion -lt [version]'3.10') {
    throw "Python 3.10 or newer is required; selected runtime is $pythonVersion."
}
$PythonPath = (Resolve-Path $PythonPath).Path
$pythonDirectory = Split-Path -Parent $PythonPath
$pythonShimDirectory = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("fingerprint-browser-python-" + [System.Guid]::NewGuid().ToString('N'))
$python3Shim = Join-Path $pythonShimDirectory 'python3.cmd'
New-Item -ItemType Directory -Path $pythonShimDirectory | Out-Null
[System.IO.File]::WriteAllText(
    $python3Shim,
    "@echo off`r`n`"$PythonPath`" %*`r`n",
    [System.Text.Encoding]::ASCII)
$env:Path = $pythonShimDirectory + [System.IO.Path]::PathSeparator +
    $pythonDirectory + [System.IO.Path]::PathSeparator + $env:Path
$env:PYTHON = $PythonPath

Push-Location $braveRoot
try {
    if (-not $SourceRoot) {
        $checkoutRoot = & $NodePath -e `
            "process.stdout.write(require('./build/commands/lib/rootDir.cjs'))"
        if ($LASTEXITCODE -ne 0 -or -not $checkoutRoot) {
            throw 'Unable to resolve the Chromium checkout root.'
        }
        $SourceRoot = Join-Path $checkoutRoot 'src'
    }
    $SourceRoot = (Resolve-Path $SourceRoot).Path

    if (-not $VisualStudioPath) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} `
            'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
            $VisualStudioPath = & $vswhere -latest -products * `
                -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                -property installationPath
        }
    }
    if ($VisualStudioPath) {
        $env:vs2022_install = $VisualStudioPath
    }
    $env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'

    $pythonPaths = @(
        (Join-Path $SourceRoot 'brave\script'),
        (Join-Path $SourceRoot 'tools\grit\grit\extern'),
        (Join-Path $SourceRoot 'brave\vendor\requests'),
        (Join-Path $SourceRoot 'brave\third_party\cryptography'),
        (Join-Path $SourceRoot 'brave\third_party\macholib'),
        (Join-Path $SourceRoot 'build'),
        (Join-Path $SourceRoot 'third_party\depot_tools')
    )
    $env:PYTHONPATH = ($pythonPaths -join [System.IO.Path]::PathSeparator)

    & $PythonPath (Join-Path $braveRoot 'build\util\version.py') update `
        (Join-Path $SourceRoot 'chrome\VERSION') --brave-version $braveVersion
    if ($LASTEXITCODE -ne 0) {
        throw 'Failed to synchronize the Chromium version file.'
    }

    $env:FINGERPRINT_BROWSER_PYTHON = $PythonPath
    $env:FINGERPRINT_BROWSER_VERSION = $braveVersion
    $env:FINGERPRINT_BROWSER_JOBS = [string]$Jobs
    $buildScript = @'
import './build/commands/lib/checkEnvironment.js'
import config from './build/commands/lib/config.ts'
import build from './build/commands/lib/build.js'

config.ignorePatchVersionNumber = false
config.braveVersion = process.env.FINGERPRINT_BROWSER_VERSION
config.androidOverrideVersionName = config.braveVersion
config.releaseTag = config.braveVersion.split('+')[0]
config.extraGnGenOpts =
  `--script-executable=${process.env.FINGERPRINT_BROWSER_PYTHON}`
await build('Release', {
  target: 'mini_installer',
  skip_signing: true,
  ninja: [`j:${process.env.FINGERPRINT_BROWSER_JOBS}`, 'k:0'],
  gn: [
    'is_component_build:false',
    'is_official_build:false',
    'is_debug:false',
    'dcheck_always_on:false',
    'enable_updater:false',
    'enable_update_notifications:false',
    'use_siso:false',
  ],
})
'@
    $buildSucceeded = $false
    for ($attempt = 1; $attempt -le $BuildAttempts; ++$attempt) {
        & $NodePath --input-type=module --eval $buildScript
        if ($LASTEXITCODE -eq 0) {
            $buildSucceeded = $true
            break
        }
        if ($attempt -lt $BuildAttempts) {
            Write-Warning `
                "Release build attempt $attempt failed; retrying incrementally."
        }
    }
    if (-not $buildSucceeded) {
        throw 'Fingerprint Browser Release build failed.'
    }

    $releaseDirectory = Join-Path $SourceRoot 'out\Release'
    $generatedArgs = Join-Path $releaseDirectory 'args_generated.gni'
    $requiredArgs = @(
        'is_component_build=false',
        'is_official_build=false',
        'is_debug=false',
        'dcheck_always_on=false',
        'enable_updater=false',
        'enable_update_notifications=false',
        'use_siso=false',
        'skip_signing=true'
    )
    $actualArgs = Get-Content -LiteralPath $generatedArgs -Raw
    foreach ($requiredArg in $requiredArgs) {
        if (-not $actualArgs.Contains($requiredArg)) {
            throw "Release build is missing required GN argument: $requiredArg"
        }
    }

    $browser = Join-Path $releaseDirectory 'brave.exe'
    $miniInstaller = Join-Path $releaseDirectory 'mini_installer.exe'
    if (-not (Test-Path -LiteralPath $browser -PathType Leaf) -or
        -not (Test-Path -LiteralPath $miniInstaller -PathType Leaf)) {
        throw 'Release browser or mini installer was not produced.'
    }

    $actualProductVersion = (Get-Item -LiteralPath $browser).VersionInfo.ProductVersion
    if ($actualProductVersion -ne $expectedProductVersion) {
        throw "Expected product version $expectedProductVersion, got $actualProductVersion."
    }
    $installerProductVersion =
        (Get-Item -LiteralPath $miniInstaller).VersionInfo.ProductVersion
    if ($installerProductVersion -ne $expectedProductVersion) {
        throw "Expected installer version $expectedProductVersion, got $installerProductVersion."
    }
    if ((Get-PeMachine -Path $browser) -ne 0x8664 -or
        (Get-PeMachine -Path $miniInstaller) -ne 0x8664) {
        throw 'Release browser and installer must both be x64 PE images.'
    }

    $signature = Get-AuthenticodeSignature -LiteralPath $miniInstaller
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::NotSigned) {
        throw "Expected an unsigned test installer, got signature status $($signature.Status)."
    }

    if (-not $DestinationDirectory) {
        $DestinationDirectory = Join-Path $releaseDirectory 'dist\windows'
    }
    New-Item -ItemType Directory -Path $DestinationDirectory -Force | Out-Null
    $artifactName = "FingerprintBrowserSetup-$expectedProductVersion-x64.exe"
    $artifact = Join-Path $DestinationDirectory $artifactName
    Copy-Item -LiteralPath $miniInstaller -Destination $artifact -Force

    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifact).Hash.ToLowerInvariant()
    $checksum = "$hash  $artifactName`n"
    $checksumPath = "$artifact.sha256"
    [System.IO.File]::WriteAllText(
        $checksumPath,
        $checksum,
        [System.Text.Encoding]::ASCII)

    [pscustomobject]@{
        Artifact = $artifact
        Checksum = $checksumPath
        ProductVersion = $actualProductVersion
        Sha256 = $hash
        Signature = [string]$signature.Status
        ReleaseType = 'Unsigned test release'
    } | Format-List
} finally {
    Pop-Location
    Remove-Item -LiteralPath $python3Shim -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $pythonShimDirectory -Force `
        -ErrorAction SilentlyContinue
}
