[CmdletBinding()]
param(
    [string]$StateRoot,
    [string]$Output,
    # Native shader pack staged into the capture's private state; without one
    # the route translates nothing and drops draws on every pack miss.
    [string]$ShaderPack,
    [string]$RenderTestScript = 'config/render-tests/fh1-race.fh1test',
    [ValidateRange(1, 3600)]
    [int]$TimeoutSeconds = 180,
    [ValidateRange(1, 32)]
    [int]$Parallel = [Math]::Max(2, [Math]::Min(16, [Environment]::ProcessorCount - 1)),
    [string[]]$GameArguments = @(),
    [switch]$SkipBuild,
    [switch]$MarkersOnly,
    [switch]$OpenInWpa
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'release-common.ps1')

$root = Get-PinyonRepoRoot
$profile = Join-Path $root 'config/performance/pinyon-critical-path.wprp'
$build = Join-Path $root 'out/build/win-amd64-relwithdebinfo'
if (-not $Output) {
    $Output = Join-Path $root ('.local/cpu-profile/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$Output = [IO.Path]::GetFullPath($Output)
[void](New-Item -ItemType Directory -Force -Path $Output)

if (-not $StateRoot) {
    $StateRoot = Join-Path $env:LOCALAPPDATA 'PinyonShift/source/0.1.0/.local/preview'
}
$StateRoot = (Resolve-Path -LiteralPath $StateRoot).Path
$profiles = @(Get-ChildItem -LiteralPath (Join-Path $StateRoot 'user') -Filter ForzaProfile -File -Recurse |
    Where-Object { $_.Directory.Name -eq 'ForzaProfile' })
if (-not $profiles.Count) { throw 'Forza profile missing in the selected state root' }
if (Get-Process pinyon_shift -ErrorAction SilentlyContinue) { throw 'Game already running' }
if ($ShaderPack) { $ShaderPack = (Resolve-Path -LiteralPath $ShaderPack).Path }

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$isAdministrator = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $MarkersOnly -and -not $isAdministrator) {
    throw 'CPU sampling and context-switch capture require an elevated PowerShell session.'
}
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-preview.ps1') -Configuration RelWithDebInfo -Parallel $Parallel
    if ($LASTEXITCODE -ne 0) { throw 'RelWithDebInfo build failed' }
}

$symbols = & (Join-Path $PSScriptRoot 'verify-profile-symbols.ps1') -BuildDirectory $build -Json |
    ConvertFrom-Json
$symbolOutput = Join-Path $Output 'symbols'
[void](New-Item -ItemType Directory -Force -Path $symbolOutput)
foreach ($module in $symbols.modules) {
    foreach ($relative in @($module.binary, $module.pdb)) {
        $source = Join-Path $root $relative
        Copy-Item -LiteralPath $source -Destination (Join-Path $symbolOutput (Split-Path $source -Leaf))
    }
}
$symbols | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Output 'symbols.json')

# Run on a private copy, as run-fh1-render-test.py does, so neither a pinned
# render seed nor the AppData save is ever written by the capture.
$runState = Join-Path $Output 'state'
if (Test-Path -LiteralPath $runState) { throw "Refusing stale capture state: $runState" }
[void](New-Item -ItemType Directory -Path $runState)
foreach ($name in @('user', 'config')) {
    $source = Join-Path $StateRoot $name
    if (Test-Path -LiteralPath $source -PathType Container) {
        Copy-Item -LiteralPath $source -Destination (Join-Path $runState $name) -Recurse
    }
}
$runCache = Join-Path $runState 'cache'
[void](New-Item -ItemType Directory -Force -Path $runCache)
foreach ($name in @('fh1-native-shaders-v2.bin', 'fh1-native-pipelines-v1.bin', 'fh1-gpu-prewarm-v3.txt')) {
    $source = Join-Path $StateRoot "cache/$name"
    if (Test-Path -LiteralPath $source -PathType Leaf) { Copy-Item -LiteralPath $source -Destination $runCache }
}
$stagedShaderPack = $null
if ($ShaderPack) {
    $python = Get-PinyonPython
    $stage = & $python (Join-Path $PSScriptRoot 'native-shader-pack.py') stage $ShaderPack --state-root $runState
    if ($LASTEXITCODE -ne 0) { throw 'Staging the shader pack failed' }
    $stagedShaderPack = ($stage | ConvertFrom-Json).destination
}

$etl = Join-Path $Output 'pinyon-shift.etl'
$wprProfile = if ($MarkersOnly) { 'PinyonCriticalPath' } else { 'PinyonCpuHotspots' }
$wprArguments = @('-start', "$profile!$wprProfile.Verbose", '-filemode')
$recording = $false
$droppedEvents = 0
$startedUtc = [DateTime]::UtcNow
try {
    & wpr @wprArguments
    if ($LASTEXITCODE -ne 0) { throw "WPR failed to start (exit $LASTEXITCODE)" }
    $recording = $true
    $launchArguments = @(
        '-NoProfile', '-File', (Join-Path $PSScriptRoot 'launch-preview.ps1'),
        '-Configuration', 'RelWithDebInfo', '-StateRoot', $runState,
        '-RenderTestScript', $RenderTestScript,
        '-RenderTestOutput', (Join-Path $Output 'render-test'),
        '-RenderTestTimeoutSeconds', "$TimeoutSeconds", '-Hidden',
        '-GameArgumentsJson', (@('--pinyon_shift_capture_performance=true',
            '--perf_log_max_mb=512') + $GameArguments | ConvertTo-Json -Compress),
        '-Json'
    )
    & (Join-Path $PSHOME 'pwsh.exe') @launchArguments |
        Set-Content -LiteralPath (Join-Path $Output 'launch.json')
    if ($LASTEXITCODE -ne 0) { throw 'Profile route failed' }
    $stopOutput = & wpr -stop $etl 'Pinyon Shift CPU hotspot capture' 2>&1 | Out-String
    $stopOutput | Set-Content -LiteralPath (Join-Path $Output 'wpr-stop.txt')
    if ($LASTEXITCODE -ne 0) { throw "WPR failed to stop (exit $LASTEXITCODE)" }
    if ($stopOutput -match 'dropped\s+(\d+)\s+events') {
        $droppedEvents = [int64]$Matches[1]
    }
    $recording = $false
}
finally {
    if ($recording) {
        & wpr -stop $etl 'Pinyon Shift partial CPU hotspot capture' | Out-Null
        if ($LASTEXITCODE -ne 0) { & wpr -cancel | Out-Null }
    }
}

$perfCapture = Get-ChildItem -LiteralPath (Join-Path $runState 'logs') -Filter '*.perf.csv' -File |
    Where-Object { $_.LastWriteTimeUtc -ge $startedUtc.AddSeconds(-2) } |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
if ($perfCapture) {
    Copy-Item -LiteralPath $perfCapture.FullName -Destination (Join-Path $Output 'frames.perf.csv')
}
# The generated sources' fingerprint: map-generated-lines.py only attributes
# samples to guest instructions against the sources this build compiled.
$codegenFingerprint = $null
$codegenStamp = Join-Path $root '.local/generated/default/codegen.stamp'
if (Test-Path -LiteralPath $codegenStamp -PathType Leaf) {
    $codegenFingerprint = (Get-Content -LiteralPath $codegenStamp -Raw | ConvertFrom-Json).fingerprint
}
$manifest = [ordered]@{
    schema = 'pinyon-shift.cpu-profile-capture.v1'
    codegen_fingerprint = $codegenFingerprint
    created_utc = [DateTime]::UtcNow.ToString('o')
    markers_only = $MarkersOnly.IsPresent
    route = (Resolve-Path -LiteralPath $RenderTestScript).Path
    source_state_root = $StateRoot
    state_root = $runState
    shader_pack = $stagedShaderPack
    etl = $etl
    symbols = 'symbols.json'
    dropped_events = $droppedEvents
    valid = ($droppedEvents -eq 0)
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Output 'capture.json')

if ($droppedEvents -ne 0) {
    throw "WPR dropped $droppedEvents events; repeat the capture before analysis."
}

if ($OpenInWpa) {
    $wpa = Get-Command wpa.exe -ErrorAction SilentlyContinue
    if (-not $wpa) { throw 'Windows Performance Analyzer is not installed' }
    Start-Process -FilePath $wpa.Source -ArgumentList @($etl)
}
$manifest
