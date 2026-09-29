[CmdletBinding()]
param(
    [ValidateSet('Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [string]$GameRoot,
    [string]$StateRoot,
    [string]$BuildDirectory,
    [string]$ShaderCaptureDir,
    [string]$DiscShaderCorpusDir,
    [string]$RenderTestScript,
    [string]$RenderTestOutput,
    [string]$RenderDocCommand,
    [string]$RenderDocCapturePrefix,
    [ValidateRange(1, 3600)]
    [int]$RenderTestTimeoutSeconds,
    [switch]$CollectFh1PassInventory,
    [switch]$SkipShaderPreparation,
    [switch]$RenderTestIncludeOpeningMovies,
    [switch]$DirectChildProcess,
    [string[]]$GameArguments = @(),
    [string]$GameArgumentsJson,
    [switch]$Json,
    [switch]$JsonEvents,
    [switch]$Hidden,
    [switch]$CrashSelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'release-common.ps1')

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$resolvedBuildDirectory = if ($BuildDirectory) {
    (Resolve-Path -LiteralPath $BuildDirectory).Path
} else {
    Join-Path $repoRoot ('out/build/win-amd64-' + $Configuration.ToLowerInvariant())
}
$executable = Join-Path $resolvedBuildDirectory 'pinyon_shift.exe'
$resolvedGameRoot = if ($GameRoot) {
    [IO.Path]::GetFullPath($GameRoot)
} else {
    Join-Path $repoRoot '.local/game/base'
}
$resolvedStateRoot = if ($StateRoot) {
    [IO.Path]::GetFullPath($StateRoot)
} else {
    Join-Path $repoRoot '.local/preview'
}

if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw 'The preview has not been built. Run tools/setup-preview.ps1 first.'
}
if (-not (Test-Path -LiteralPath (Join-Path $resolvedGameRoot 'default.xex') -PathType Leaf)) {
    throw "Game files are missing at $resolvedGameRoot. Select your disc image in the launcher and run setup to restore them. Your save will be preserved."
}
if (@(Get-Process -Name 'pinyon_shift' -ErrorAction SilentlyContinue).Count -ne 0) {
    throw 'Pinyon Shift is already running.'
}

foreach ($directory in @('', 'cache', 'config', 'crashes', 'logs', 'reports', 'update', 'user')) {
    $path = if ($directory) { Join-Path $resolvedStateRoot $directory } else { $resolvedStateRoot }
    [void](New-Item -ItemType Directory -Force -Path $path)
}
$pendingReport = Join-Path $resolvedStateRoot 'reports/pending-report.json'
if (Test-Path -LiteralPath $pendingReport -PathType Leaf) {
    Remove-Item -LiteralPath $pendingReport -Force
}

$stagedNativeShaderPack = $null
$stagedNativePipelineCache = $null
$stagedShaderProducer = $null
if ($DiscShaderCorpusDir) {
    # Plugins carry the build type's postfix (PluginFileName in the SDK).
    $buildType = 'Release'
    $cache = Join-Path $resolvedBuildDirectory 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cache -PathType Leaf) {
        $line = Select-String -LiteralPath $cache -Pattern '^CMAKE_BUILD_TYPE:STRING=(.+)$' |
            Select-Object -First 1
        if ($line) { $buildType = $line.Matches[0].Groups[1].Value }
    }
    $pluginPostfix = switch ($buildType) { 'Debug' { 'd' } 'RelWithDebInfo' { 'rd' } default { '' } }
    $producerName = "rexgpu-fh1-producer$pluginPostfix.dll"
    $producerSource = Join-Path $resolvedBuildDirectory "rexglue-artifacts/$producerName"
    if (-not (Test-Path -LiteralPath $producerSource -PathType Leaf)) {
        throw 'Build the rexgpu-fh1-producer target before producing FH1 shaders.'
    }
    $stagedShaderProducer = Join-Path (Split-Path $executable -Parent) $producerName
}
if (-not ($RenderTestScript -or $ShaderCaptureDir -or $DiscShaderCorpusDir -or $CrashSelfTest -or $SkipShaderPreparation)) {
    & (Join-Path $PSScriptRoot 'prepare-fh1-shaders.ps1') -StateRoot $resolvedStateRoot `
        -GameRoot $resolvedGameRoot -BuildDirectory $resolvedBuildDirectory -JsonEvents:$JsonEvents
    $stagedNativeShaderPack = Join-Path $resolvedStateRoot 'cache/fh1-artifacts.json'
    $stagedNativePipelineCache = Join-Path $resolvedStateRoot 'cache'
}

# Mods' database patches (NP-10.2) are applied to a copy of the player's own
# gamedb.slt before each start, so they follow the enabled mods.
if (Test-Path -LiteralPath (Join-Path $resolvedStateRoot 'mods') -PathType Container) {
    & (Get-PinyonPython) (Join-Path $PSScriptRoot 'build-mod-patches.py') $resolvedStateRoot `
        --game-root $resolvedGameRoot | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Could not build the mods'' database patches.' }
    # Mods' single archive members (NP-10.1), rebuilt into copies of the
    # player's archives with zipmanifest.xml to match.
    & (Get-PinyonPython) (Join-Path $PSScriptRoot 'build-mod-archives.py') $resolvedStateRoot `
        --game-root $resolvedGameRoot | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Could not build the mods'' archive members.' }
}

$savedStateRoot = $env:PINYON_SHIFT_STATE_ROOT
$savedGameRoot = $env:PINYON_SHIFT_GAME_ROOT
$savedTearing = $env:REX_D3D12_ALLOW_VARIABLE_REFRESH_RATE_AND_TEARING
$savedCrashTest = $env:PINYON_SHIFT_CRASH_SELF_TEST
$savedShaderCaptureDir = $env:PINYON_SHIFT_NATIVE_SHADER_CAPTURE_DIR
$savedDiscShaderCorpusDir = $env:PINYON_SHIFT_FH1_DISC_SHADER_CORPUS_DIR
$savedRenderTestScript = $env:PINYON_SHIFT_FH1_RENDER_TEST_SCRIPT
$savedRenderTestOutput = $env:PINYON_SHIFT_FH1_RENDER_TEST_OUTPUT
$savedWindowHidden = $env:REX_WINDOW_HIDDEN
$startedUtc = [DateTime]::UtcNow
$process = $null
try {
    $env:REX_WINDOW_HIDDEN = if ($Hidden) { '1' } else { $null }
    if ($stagedShaderProducer) {
        Copy-Item -LiteralPath $producerSource -Destination $stagedShaderProducer -Force
    }
    $env:PINYON_SHIFT_STATE_ROOT = $resolvedStateRoot
    $env:PINYON_SHIFT_GAME_ROOT = $resolvedGameRoot
    $env:REX_D3D12_ALLOW_VARIABLE_REFRESH_RATE_AND_TEARING = 'false'
    $env:PINYON_SHIFT_CRASH_SELF_TEST = if ($CrashSelfTest) { '1' } else { $null }
    $env:PINYON_SHIFT_NATIVE_SHADER_CAPTURE_DIR = if ($ShaderCaptureDir) {
        [IO.Path]::GetFullPath($ShaderCaptureDir)
    } else {
        $null
    }
    $env:PINYON_SHIFT_FH1_DISC_SHADER_CORPUS_DIR = if ($DiscShaderCorpusDir) {
        (Resolve-Path -LiteralPath $DiscShaderCorpusDir).Path
    } else {
        $null
    }
    $env:PINYON_SHIFT_FH1_RENDER_TEST_SCRIPT = if ($RenderTestScript) {
        (Resolve-Path -LiteralPath $RenderTestScript).Path
    } else {
        $null
    }
    $env:PINYON_SHIFT_FH1_RENDER_TEST_OUTPUT = if ($RenderTestOutput) {
        [IO.Path]::GetFullPath($RenderTestOutput)
    } else {
        $null
    }
    $start = @{
        FilePath = $executable
        WorkingDirectory = (Split-Path $executable -Parent)
        PassThru = $true
    }
    $normalizedGameArguments = @($GameArguments)
    if ($Hidden) {
        if (-not $DirectChildProcess) { $start.WindowStyle = 'Hidden' }
        $normalizedGameArguments += '--audio_mute=true'
    }
    if ($GameArgumentsJson) {
        foreach ($gameArgument in (ConvertFrom-Json -InputObject $GameArgumentsJson)) {
            $normalizedGameArguments += [string]$gameArgument
        }
    }
    if ($RenderTestScript) {
        if (-not $RenderTestIncludeOpeningMovies) {
            $normalizedGameArguments += '--pinyon_shift_skip_opening_movies=true'
        }
    }
    if ($CollectFh1PassInventory) {
        $normalizedGameArguments += '--pinyon_shift_fh1_gpu_corpus=true'
    }
    if ($normalizedGameArguments.Count -ne 0) {
        $start.ArgumentList = $normalizedGameArguments
    }
    if ($RenderDocCommand) {
        if (-not $RenderDocCapturePrefix) {
            throw '-RenderDocCapturePrefix is required with -RenderDocCommand.'
        }
        $start.FilePath = (Resolve-Path -LiteralPath $RenderDocCommand).Path
        $start.ArgumentList = @('capture', '--wait-for-exit',
            '-d', (Split-Path $executable -Parent), '-c',
            [IO.Path]::GetFullPath($RenderDocCapturePrefix), $executable) +
            $normalizedGameArguments
    }
    if ($DirectChildProcess) {
        # Keep capture/debugger child-process hooks on the launching process.
        $start.NoNewWindow = $true
    }
    if ($JsonEvents) { Write-PinyonEvent play 100 'Starting game.' -JsonEvents }
    $process = Start-Process @start
    if ($DirectChildProcess) {
        # Cache the live handle so ExitCode remains available after exit.
        $null = $process.Handle
    }
    if ($RenderTestTimeoutSeconds) {
        if (-not $process.WaitForExit($RenderTestTimeoutSeconds * 1000)) {
            Stop-Process -Id $process.Id -Force
            $process.WaitForExit()
            throw "Pinyon Shift render test timed out after $RenderTestTimeoutSeconds seconds."
        }
    } else {
        $process.WaitForExit()
    }
    $process.Refresh()
}
finally {
    $env:PINYON_SHIFT_STATE_ROOT = $savedStateRoot
    $env:PINYON_SHIFT_GAME_ROOT = $savedGameRoot
    $env:REX_D3D12_ALLOW_VARIABLE_REFRESH_RATE_AND_TEARING = $savedTearing
    $env:PINYON_SHIFT_CRASH_SELF_TEST = $savedCrashTest
    $env:PINYON_SHIFT_NATIVE_SHADER_CAPTURE_DIR = $savedShaderCaptureDir
    $env:PINYON_SHIFT_FH1_DISC_SHADER_CORPUS_DIR = $savedDiscShaderCorpusDir
    $env:PINYON_SHIFT_FH1_RENDER_TEST_SCRIPT = $savedRenderTestScript
    $env:PINYON_SHIFT_FH1_RENDER_TEST_OUTPUT = $savedRenderTestOutput
    $env:REX_WINDOW_HIDDEN = $savedWindowHidden
    if ($stagedShaderProducer) {
        Remove-Item -LiteralPath $stagedShaderProducer -Force -ErrorAction SilentlyContinue
    }
}

if ($null -eq $process) { throw 'Windows did not start Pinyon Shift.' }
$exitCode = [int64]$process.ExitCode
if ($RenderDocCommand) {
    $result = [ordered]@{
        result = if ($exitCode -eq 0) { 'capture-wrapper-exit' } else { 'capture-wrapper-failed' }
        process_id = $process.Id
        exit_code = $exitCode
        capture_prefix = [IO.Path]::GetFullPath($RenderDocCapturePrefix)
    }
    if ($Json) { $result | ConvertTo-Json -Compress } else { $result }
    if ($exitCode -ne 0) { exit 1 }
    return
}
if ($exitCode -ne 0) {
    $report = & (Join-Path $PSScriptRoot 'create-crash-report.ps1') `
        -StateRoot $resolvedStateRoot -Executable $executable `
        -StartedUtc $startedUtc -ProcessId $process.Id -ExitCode $exitCode -Json |
        ConvertFrom-Json
    $result = [ordered]@{
        result = 'crash'
        process_id = $process.Id
        exit_code = $exitCode
        crash_id = $report.crash_id
        bundle = $report.bundle
        issue_url = $report.issue_url
    }
    if ($Json) { $result | ConvertTo-Json -Compress } else { $result }
    exit 1
}

$result = [ordered]@{
    result = 'normal-exit'
    process_id = $process.Id
    exit_code = $exitCode
    native_shader_pack = $stagedNativeShaderPack
    native_pipeline_cache = $stagedNativePipelineCache
}
if ($Json) { $result | ConvertTo-Json -Compress } else { $result }
