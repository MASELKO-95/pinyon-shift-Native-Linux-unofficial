[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$WorkRoot,
    [Parameter(Mandatory)] [string]$RenderTestScript,
    [string]$GameRoot,
    [string]$RuntimeConfig,
    [string]$BuildDirectory,
    [string]$SeedShaderCacheRoot,
    # Pack misses recorded by earlier runs (<state>/cache/fh1-shader-misses):
    # title-generated shaders the empty-profile route does not reach.
    [string]$ShaderMissDir,
    [switch]$Hidden,
    [switch]$AllowPipelineDiscovery,
    [ValidateRange(1, 3)] [int]$Scale = 1,
    [switch]$IncludeOpeningMovies,
    [switch]$JsonEvents
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'release-common.ps1')
$root = Get-PinyonRepoRoot
$work = Resolve-PinyonLocalPath -RelativePath $WorkRoot
# ponytail: restart-only production; preserve failed work for diagnosis until
# receipt-based resume can prove the inputs and every intermediate unchanged.
if (Test-Path -LiteralPath $work) { throw 'Use a new .local work directory; existing production is never overwritten.' }
if (Get-Process pinyon_shift -ErrorAction SilentlyContinue) { throw 'Close the preview before producing renderer artifacts.' }
$game = if ($GameRoot) { (Resolve-Path -LiteralPath $GameRoot).Path } else { Join-Path $root '.local/game/base' }
$script = (Resolve-Path -LiteralPath $RenderTestScript).Path
$python = Get-PinyonPython
$dump = (Get-Content (Join-Path $root 'config/supported-dumps.json') -Raw | ConvertFrom-Json).dumps[0]
foreach ($entry in $dump.executables) {
    $path = Join-Path $game $entry.guest_path
    if ((Get-Item -LiteralPath $path).Length -ne $entry.size_bytes -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw 'The extracted game executables do not match the supported FH1 revision.'
    }
}
[void](New-Item -ItemType Directory -Path $work)
$build = if ($BuildDirectory) { (Resolve-Path -LiteralPath $BuildDirectory).Path } else { Join-Path $root 'out/build/win-amd64-release' }
$environment = Enter-PinyonBuildEnvironment
Write-PinyonEvent shaders 0 'Building the offline shader producer.' -JsonEvents:$JsonEvents
& $environment.CMake --build $build --config Release --target rexgpu-fh1-producer pinyon_shift_fh1_archive_extract *> (Join-Path $work 'build.log')
if ($LASTEXITCODE) { throw "Producer build failed. See $work/build.log." }
Write-PinyonEvent shaders 10 'Extracting shader programs from the local game files.' -JsonEvents:$JsonEvents
& $python (Join-Path $PSScriptRoot 'extract-fh1-shader-corpus.py') $game `
    --output (Join-Path $work 'corpus.json') --binary-dir (Join-Path $work 'corpus') `
    --archive-extractor (Join-Path $build 'pinyon_shift_fh1_archive_extract.exe') *> (Join-Path $work 'extract.log')
if ($LASTEXITCODE) { throw "Shader extraction failed. See $work/extract.log." }

# Each phase starts with its own empty state. Never copy a player's save or
# borrow shader caches from a developer installation.
$arguments = @("--draw_resolution_scale_x=$Scale", "--draw_resolution_scale_y=$Scale")
$launch = @{
    GameRoot = $game; BuildDirectory = $build; RenderTestScript = $script; RenderTestTimeoutSeconds = 600
    RenderTestIncludeOpeningMovies = $IncludeOpeningMovies; CollectFh1PassInventory = $true
    GameArguments = $arguments; Json = $true
    Hidden = $Hidden
}
Write-PinyonEvent shaders 20 'Producing shaders and collecting startup pipelines.' -JsonEvents:$JsonEvents
$producerState = Join-Path $work 'producer-state'
if ($SeedShaderCacheRoot) {
    $seedDirectory = Join-Path $producerState 'cache/shaders/shareable'
    [void][IO.Directory]::CreateDirectory($seedDirectory)
    foreach ($name in @('4D5309C9.xsh', '4D5309C9.rtv.d3d12.xpso')) {
        Copy-Item -LiteralPath (Join-Path $SeedShaderCacheRoot $name) -Destination $seedDirectory
    }
}
if ($ShaderMissDir) {
    $missDirectory = Join-Path $producerState 'cache/fh1-shader-misses'
    [void][IO.Directory]::CreateDirectory($missDirectory)
    Get-ChildItem -LiteralPath $ShaderMissDir -Filter '*.bin' |
        Where-Object Name -Match '^(vertex|pixel)-[0-9A-F]{16}-[0-9A-F]{16}\.bin$' |
        Copy-Item -Destination $missDirectory
}
if ($RuntimeConfig -and (Test-Path -LiteralPath $RuntimeConfig)) {
    foreach ($phase in @('producer-state', 'strict-state')) {
        $configDirectory = Join-Path $work "$phase/config"
        [void][IO.Directory]::CreateDirectory($configDirectory)
        Copy-Item -LiteralPath $RuntimeConfig -Destination (Join-Path $configDirectory 'pinyon_shift.toml')
    }
}
$producer = & (Join-Path $PSScriptRoot 'launch-preview.ps1') @launch -StateRoot $producerState `
    -DiscShaderCorpusDir (Join-Path $work 'corpus') -ShaderCaptureDir (Join-Path $work 'translation') `
    -RenderTestOutput (Join-Path $work 'producer-output') | ConvertFrom-Json
if ($producer.result -ne 'normal-exit') { throw 'The offline producer did not exit normally.' }
$events = @(Get-Content (Join-Path $producerState 'logs/*.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
$summary = @($events | Where-Object event -eq 'native_renderer.shader_capture.summary')
$complete = @($events | Where-Object event -eq 'fh1.render_test.complete')
if ($summary.Count -ne 1 -or $complete.Count -ne 1 -or
    [int]$summary[0].rejected_callbacks -ne 0 -or [int]$summary[0].entries -eq 0) {
    throw 'Shader production did not provide complete, error-free capture evidence.'
}
$runtimeLog = Get-Content (Join-Path $producerState 'logs/runtime*.log') -Raw
if (-not ($runtimeLog -match 'FH1 disc corpus translated \d+ vertex and \d+ pixel shader variants with 0 failures')) {
    throw 'The disc shader translation did not finish successfully.'
}
Write-PinyonEvent shaders 75 'Validating the shader pack and startup catalogs.' -JsonEvents:$JsonEvents
$packPath = Join-Path $work 'shaders.pnsp'
$packJson = & $python (Join-Path $PSScriptRoot 'native-shader-pack.py') build `
    (Join-Path $work 'translation/shader-manifest.json') --output $packPath
if ($LASTEXITCODE) { throw 'Shader pack validation failed.' }
$pack = $packJson | ConvertFrom-Json
if ($pack.entry_count -ne [int]$summary[0].entries -or
    $pack.draw_resolution_scale_x -ne $Scale -or $pack.draw_resolution_scale_y -ne $Scale) {
    throw 'The shader pack does not match the producer evidence or requested scale.'
}
$strictState = Join-Path $work 'strict-state'
$corpora = @(Get-ChildItem (Join-Path $producerState 'cache/fh1-gpu-corpus') -Filter '*.json')
if ($corpora.Count -ne 1) { throw 'Expected one producer execution corpus.' }
& $python (Join-Path $PSScriptRoot 'build-fh1-gpu-prewarm.py') $corpora[0].FullName `
    (Join-Path $strictState 'cache/fh1-gpu-prewarm-v3.txt') --legacy-cache (Join-Path $producerState 'cache') *> (Join-Path $work 'catalog.log')
if ($LASTEXITCODE) { throw 'Startup catalog construction failed.' }
& $python (Join-Path $PSScriptRoot 'native-shader-pack.py') stage $packPath --state-root $strictState --scale $Scale *> (Join-Path $work 'stage.json')
if ($LASTEXITCODE) { throw 'Shader pack staging failed.' }
Write-PinyonEvent shaders 90 'Checking the captured route with the compiler-free renderer.' -JsonEvents:$JsonEvents
$strict = & (Join-Path $PSScriptRoot 'launch-preview.ps1') @launch -StateRoot $strictState `
    -RenderTestOutput (Join-Path $work 'strict-output') | ConvertFrom-Json
if ($strict.result -ne 'normal-exit') { throw 'The compiler-free route failed.' }
$events = @(Get-Content (Join-Path $strictState 'logs/*.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
$strictLog = Get-Content (Join-Path $strictState 'logs/runtime*.log') -Raw
if (@($events | Where-Object event -eq 'fh1.render_test.complete').Count -ne 1) {
    throw 'The compiler-free route did not complete.'
}
# The native renderer draws through the pack's pipelines directly; its
# executor must have rendered the route. Runtime translations show up as pack
# misses, checked below.
if (-not ($strictLog -match 'FH1 native executor enabled: native')) {
    throw 'The native renderer was not enabled for the compiler-free route.'
}
$stats = [regex]::Matches($strictLog, 'FH1 native executor frame=(\d+) draws=(\d+) resolves=(\d+)')
if ($stats.Count -eq 0 -or [int64]$stats[$stats.Count - 1].Groups[2].Value -eq 0) {
    throw 'The native renderer did not execute the route.'
}
$last = $stats[$stats.Count - 1]
$execution = [ordered]@{ renderer = 'native'; frames = [int64]$last.Groups[1].Value
    draws = [int64]$last.Groups[2].Value; resolves = [int64]$last.Groups[3].Value }
if ($strictLog -match 'FH1 precompiled shader pack miss' -or
    -not ($strictLog -match "Loaded $($pack.entry_count) FH1 precompiled shaders")) {
    throw 'The compiler-free route did not load and use the produced shader pack without misses.'
}
$report = [ordered]@{
    schema_version = 1
    result = if ($AllowPipelineDiscovery) { 'shaders-validated' } else { 'route-validated' }
    # New D3D12 pipeline states can use prepared bytecode without translating
    # game shaders. Record this distinction instead of claiming complete PSO coverage.
    pipeline_discovery_allowed = [bool]$AllowPipelineDiscovery
    gameplay_ready = $false
    dump_id = $dump.id; render_test_sha256 = (Get-FileHash -LiteralPath $script).Hash
    corpus_sha256 = (Get-FileHash -LiteralPath (Join-Path $work 'corpus.json')).Hash
    pack = $pack; producer_pid = $producer.process_id; strict_pid = $strict.process_id
    runtime_sha256 = (Get-FileHash -LiteralPath (Join-Path $build 'rexgpu-fh1.dll')).Hash
    producer_sha256 = (Get-FileHash -LiteralPath (Join-Path $build 'rexglue-artifacts/rexgpu-fh1-producer.dll')).Hash
    executable_sha256 = (Get-FileHash -LiteralPath (Join-Path $build 'pinyon_shift.exe')).Hash
    execution = $execution
}
[IO.File]::WriteAllText((Join-Path $work 'production.json'), ($report | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
Write-PinyonEvent shaders 100 'Graphics validation finished.' -JsonEvents:$JsonEvents
$report | ConvertTo-Json -Depth 6
