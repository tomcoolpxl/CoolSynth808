# The release pipeline's steps (CoolSynth's D363), as CoolSynth's own scripts/ci/Common.ps1: configure, build, test,
# then the packages: one zip of the app, the VST3 and the notices, the installer, and their checksums.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-PathExists {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Description not found: $Path"
    }
}

function New-Cs808BuildContext {
    param(
        [Parameter(Mandatory)][string]$SourceRoot,
        [Parameter(Mandatory)][ValidateSet('Debug', 'Release')][string]$Configuration,
        [string]$TagName = ''
    )

    $resolvedSourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
    $configurationKey = $Configuration.ToLowerInvariant()
    $buildDirectory = Join-Path $resolvedSourceRoot "build/ci-$configurationKey"

    [pscustomobject]@{
        SourceRoot       = $resolvedSourceRoot
        Configuration    = $Configuration
        BuildDirectory   = $buildDirectory
        LogsDirectory    = Join-Path $buildDirectory 'logs'
        ArtifactRoot     = Join-Path $buildDirectory "CoolSynth808_artefacts/$Configuration"
        TagName          = if ([string]::IsNullOrWhiteSpace($TagName)) { "manual-$configurationKey" } else { $TagName }
        PackageDirectory = Join-Path $buildDirectory 'packages'
        TestResults      = Join-Path $buildDirectory 'Testing/ctest-results.xml'
        CommitSha        = if ($env:GITHUB_SHA) { $env:GITHUB_SHA } else { '' }
    }
}

function Invoke-LoggedCommand {
    param(
        [Parameter(Mandatory)][string]$LogPath,
        [Parameter(Mandatory)][scriptblock]$ScriptBlock
    )

    New-Item -ItemType Directory -Path (Split-Path -Path $LogPath -Parent) -Force | Out-Null
    & $ScriptBlock 2>&1 | Tee-Object -FilePath $LogPath
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed. See log: $LogPath"
    }
}

function Invoke-Cs808Configure {
    param([Parameter(Mandatory)][pscustomobject]$BuildContext)

    Invoke-LoggedCommand -LogPath (Join-Path $BuildContext.LogsDirectory 'configure.log') -ScriptBlock {
        # Without ASIO, even where Steinberg's SDK is on the computer: its GPLv3 cannot go into a binary with JUCE's
        # Starter licence, and its other licence needs Steinberg's signed agreement before a binary is published.
        & cmake -S $BuildContext.SourceRoot -B $BuildContext.BuildDirectory -G 'Visual Studio 17 2022' -A x64 -DCOOLSYNTH808_ASIO=OFF
    }
}

function Invoke-Cs808Build {
    # Every target: the app, the VST3 and the tests (the hosting test loads the VST3 just built).
    param([Parameter(Mandatory)][pscustomobject]$BuildContext)

    Invoke-LoggedCommand -LogPath (Join-Path $BuildContext.LogsDirectory 'build.log') -ScriptBlock {
        & cmake --build $BuildContext.BuildDirectory --config $BuildContext.Configuration --parallel
    }
}

function Invoke-Cs808Tests {
    param([Parameter(Mandatory)][pscustomobject]$BuildContext)

    New-Item -ItemType Directory -Path (Split-Path -Path $BuildContext.TestResults -Parent) -Force | Out-Null
    Invoke-LoggedCommand -LogPath (Join-Path $BuildContext.LogsDirectory 'ctest.log') -ScriptBlock {
        & ctest --test-dir $BuildContext.BuildDirectory -C $BuildContext.Configuration --output-on-failure --output-junit $BuildContext.TestResults
    }
    Assert-PathExists -Path $BuildContext.TestResults -Description 'CTest JUnit results'
}

function Get-Cs808Version {
    param([Parameter(Mandatory)][pscustomobject]$BuildContext)

    $cmake = Get-Content -LiteralPath (Join-Path $BuildContext.SourceRoot 'CMakeLists.txt') -Raw
    if ($cmake -notmatch 'project\(\s*CoolSynth808\s+VERSION\s+(\d+\.\d+\.\d+)') {
        throw 'No project version in CMakeLists.txt.'
    }
    return $Matches[1]
}

function New-Cs808Asset {
    param([Parameter(Mandatory)][string]$LogicalName, [Parameter(Mandatory)][string]$Path)

    [pscustomobject]@{
        LogicalName = $LogicalName
        FileName    = Split-Path -Path $Path -Leaf
        FilePath    = $Path
        Sha256      = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

function New-Cs808Zip {
    # One zip: the app, the VST3 bundle, the licence and the notices, for those who would rather not run an installer.
    param([Parameter(Mandatory)][pscustomobject]$BuildContext)

    $exe = Join-Path $BuildContext.ArtifactRoot 'Standalone/CoolSynth 808.exe'
    $vst3 = Join-Path $BuildContext.ArtifactRoot 'VST3/CoolSynth 808.vst3'
    Assert-PathExists -Path $exe -Description 'Standalone app'
    Assert-PathExists -Path $vst3 -Description 'VST3 bundle'

    if (Test-Path -LiteralPath $BuildContext.PackageDirectory) {
        Remove-Item -LiteralPath $BuildContext.PackageDirectory -Recurse -Force
    }
    $stage = Join-Path $BuildContext.PackageDirectory '.staging'
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    Copy-Item -LiteralPath $exe -Destination $stage
    Copy-Item -LiteralPath $vst3 -Destination $stage -Recurse
    Copy-Item -LiteralPath (Join-Path $BuildContext.SourceRoot 'LICENSE') -Destination (Join-Path $stage 'LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $BuildContext.SourceRoot 'THIRD_PARTY_NOTICES.txt') -Destination $stage

    $zip = Join-Path $BuildContext.PackageDirectory "CoolSynth808-windows-x64-$($BuildContext.TagName).zip"
    Compress-Archive -Path (Get-ChildItem -LiteralPath $stage -Force | Select-Object -ExpandProperty FullName) -DestinationPath $zip -CompressionLevel Optimal -Force
    Remove-Item -LiteralPath $stage -Recurse -Force
    Assert-PathExists -Path $zip -Description 'Zip'
    New-Cs808Asset -LogicalName 'zip' -Path $zip
}

function New-Cs808Installer {
    # installer/CoolSynth808.iss compiled with Inno Setup 6.5 or newer (ISCC on the PATH, or in its usual folders).
    param([Parameter(Mandatory)][pscustomobject]$BuildContext)

    $iscc = (Get-Command ISCC -ErrorAction SilentlyContinue).Source
    if (-not $iscc) {
        $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") |
            Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    }
    if (-not $iscc) {
        throw 'Inno Setup (ISCC) not found: install Inno Setup 6.5 or newer.'
    }

    $version = Get-Cs808Version -BuildContext $BuildContext
    $baseName = "CoolSynth808-Setup-$($BuildContext.TagName)"
    $script = Join-Path $BuildContext.SourceRoot 'installer/CoolSynth808.iss'
    $buildDir = $BuildContext.ArtifactRoot
    $packageDir = $BuildContext.PackageDirectory
    # ISCC's lines go to the screen and the log, not into this function's result (which must be the one package).
    Invoke-LoggedCommand -LogPath (Join-Path $BuildContext.LogsDirectory 'installer.log') -ScriptBlock {
        & $iscc "/DAppVersion=$version" "/DBuildDir=$buildDir" "/O$packageDir" "/F$baseName" $script
    } | Out-Host

    $installer = Join-Path $BuildContext.PackageDirectory "$baseName.exe"
    Assert-PathExists -Path $installer -Description 'Installer'
    New-Cs808Asset -LogicalName 'installer' -Path $installer
}

function Write-Cs808ChecksumFile {
    param(
        [Parameter(Mandatory)][array]$PackagedAssets,
        [Parameter(Mandatory)][string]$DestinationPath
    )

    $lines = foreach ($asset in $PackagedAssets) { '{0} *{1}' -f $asset.Sha256, $asset.FileName }
    Set-Content -LiteralPath $DestinationPath -Value $lines -Encoding ascii
    Assert-PathExists -Path $DestinationPath -Description 'Checksum file'
}
