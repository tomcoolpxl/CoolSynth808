# Configures and builds CoolSynth 808 (the app, the VST3 and the tests) in build/ci-<configuration>, and runs the
# whole test program when asked (CoolSynth's D363, as CoolSynth's own BuildAndTest.ps1).
param(
    [Parameter(Mandatory)][ValidateSet('Debug', 'Release')][string]$Configuration,
    [switch]$RunTests
)

. "$PSScriptRoot/Common.ps1"

$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$context = New-Cs808BuildContext -SourceRoot $sourceRoot -Configuration $Configuration

Write-Host "Configuration: $($context.Configuration)"
Write-Host "Build directory: $($context.BuildDirectory)"

Invoke-Cs808Configure -BuildContext $context
Invoke-Cs808Build -BuildContext $context
if ($RunTests) {
    Invoke-Cs808Tests -BuildContext $context
}
