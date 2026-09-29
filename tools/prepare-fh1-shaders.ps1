[CmdletBinding()]
param(
    [string]$StateRoot,
    [string]$GameRoot,
    [string]$BuildDirectory,
    [switch]$JsonEvents
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'release-common.ps1')
$root = Get-PinyonRepoRoot
if (-not $StateRoot) { $StateRoot = $env:PINYON_SHIFT_STATE_ROOT }
if (-not $StateRoot) { $StateRoot = Join-Path $root '.local/preview' }
$StateRoot = [IO.Path]::GetFullPath($StateRoot)
if (-not $GameRoot) { $GameRoot = Join-Path $root '.local/game/base' }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $root 'out/build/win-amd64-release' }
if (Get-Process pinyon_shift -ErrorAction SilentlyContinue) { throw 'Close the game before preparing graphics.' }
$cache = Join-Path $StateRoot 'cache'
[void][IO.Directory]::CreateDirectory($cache)
try { $lock = [IO.File]::Open((Join-Path $cache 'fh1-preparation.lock'), 'OpenOrCreate', 'ReadWrite', 'None') }
catch { throw 'Another launcher is preparing graphics for this installation. Wait for it to finish.' }

# Replaces or creates $Path with $Temporary. A 500 MB pack just written is
# often still open in a real-time antivirus scan, so retry briefly.
function Move-IntoPlace($Temporary, $Path) {
    for ($attempt = 1; ; $attempt++) {
        try {
            if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($Temporary, $Path, [NullString]::Value) }
            else { [IO.File]::Move($Temporary, $Path) }
            return
        } catch [IO.IOException] {
            if ($attempt -ge 10) { throw }
            Start-Sleep -Milliseconds 500
        }
    }
}

function Write-AtomicJson($Path, $Value) {
    [void][IO.Directory]::CreateDirectory((Split-Path $Path -Parent))
    $temporary = "$Path.$([Guid]::NewGuid().ToString('N')).tmp"
    [IO.File]::WriteAllText($temporary, ($Value | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    Move-IntoPlace $temporary $Path
}

function Read-Receipt($Path) {
    try { Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json }
    catch { return $null }
}

function Test-ArtifactSet($Receipt, $Directory, $Key) {
    try {
        if ($null -eq $Receipt -or $Receipt.schema_version -ne 1 -or $Receipt.key -ne $Key -or
            @($Receipt.files).Count -ne 4) { return $false }
        $names = @($Receipt.files | ForEach-Object { $_.path })
        if (@($names | Select-Object -Unique).Count -ne 4) { return $false }
        foreach ($required in @('fh1-gpu-prewarm-v3.txt', 'fh1-native-shaders-v2.bin', 'fh1-native-pipelines-v1.bin')) {
            if ($required -notin $names) { return $false }
        }
        if (@($names | Where-Object { $_ -match '^shaders/shareable/[^/\\]+\.pnsp$' }).Count -ne 1) { return $false }
        $prefix = [IO.Path]::GetFullPath($Directory).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
        foreach ($file in $Receipt.files) {
            $path = [IO.Path]::GetFullPath((Join-Path $Directory $file.path))
            if (-not $path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
                -not (Test-Path -LiteralPath $path -PathType Leaf) -or
                (Get-FileHash -LiteralPath $path).Hash -ne $file.sha256) { return $false }
        }
        return $true
    } catch { return $false }
}

try {
    $settings = [ordered]@{
        d3d12_bindless = 'true'; gamma_render_target_as_unorm16 = 'true'; d3d12_adapter = '-1'
        draw_resolution_scale_x = '1'; draw_resolution_scale_y = '1'
    }
    $config = Join-Path $StateRoot 'config/pinyon_shift.toml'
    if (Test-Path -LiteralPath $config) {
        $text = Get-Content -LiteralPath $config -Raw
        foreach ($name in @($settings.Keys)) {
            $match = [regex]::Match($text, "(?m)^\s*$name\s*=\s*([^#\r\n]+)")
            if ($match.Success) { $settings[$name] = $match.Groups[1].Value.Trim().ToLowerInvariant() }
        }
    }
    $scale = [int]$settings.draw_resolution_scale_x
    if ($scale -lt 1 -or $scale -gt 4 -or [int]$settings.draw_resolution_scale_y -ne $scale) {
        throw 'Choose a matching 1x, 2x, 3x or 4x graphics resolution before preparing shaders.'
    }
    $inputs = [ordered]@{ scale = $scale; settings = $settings; gpu = @(
        Get-CimInstance Win32_VideoController | Sort-Object PNPDeviceID |
            Select-Object PNPDeviceID, DriverVersion
    ); files = [ordered]@{} }
    foreach ($relative in @(
        'config/release-toolchain.json', 'config/supported-dumps.json',
        'config/render-tests/fh1-shader-preparation.fh1test',
        'tools/prepare-fh1-shaders.ps1', 'tools/produce-fh1-artifacts.ps1',
        'tools/extract-fh1-shader-corpus.py', 'tools/fh1_archive_extract.cpp',
        'tools/build-fh1-gpu-prewarm.py',
        'tools/native-shader-pack.py'
    )) { $inputs.files[$relative] = (Get-FileHash -LiteralPath (Join-Path $root $relative)).Hash }
    # Key the pack and catalogs on the sources that decide their content (the
    # translator, shader analysis, pipeline descriptions, the pack format and
    # the capture) instead of the built binaries, so a rebuild of host code, the
    # guest hooks or the renderer's command path keeps the prepared graphics. A
    # shader such a rebuild newly reaches is recorded as a pack miss and
    # prepared on the next launch; a stale pipeline catalog only costs a
    # pipeline created on first use.
    $sdk = 'thirdparty/shiftglue-sdk'
    $rootPrefix = [IO.Path]::GetFullPath($root).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    $graphicsSources = [Collections.Generic.List[string]]::new()
    foreach ($directory in @("$sdk/include/rex/graphics/pipeline/shader", "$sdk/src/graphics/pipeline/shader")) {
        foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root $directory) -File) {
            $relative = $file.FullName.Substring($rootPrefix.Length).Replace([IO.Path]::DirectorySeparatorChar, '/')
            if ($relative -notmatch '(?i)spirv') { $graphicsSources.Add($relative) }
        }
    }
    foreach ($relative in @(
        'd3d12/fh1_shader_pack', 'd3d12/host_render_config', 'd3d12/pipeline_cache', 'd3d12/primitive_processor',
        'd3d12/shader', 'flags', 'format/ucode', 'primitive_processor', 'registers', 'util/draw', 'xenos'
    )) {
        $graphicsSources.Add("$sdk/include/rex/graphics/$relative.h")
        $graphicsSources.Add("$sdk/src/graphics/$relative.cpp")
    }
    $graphicsSources.AddRange([string[]]@(
        "$sdk/include/rex/graphics/pipeline/render_target/psi_color_format.h",
        "$sdk/include/rex/graphics/pipeline_util.h", "$sdk/include/rex/graphics/register_table.inc",
        'src/native_renderer/shader_capture.cpp'
    ))
    foreach ($relative in @($graphicsSources | Sort-Object -Unique -CaseSensitive)) {
        $inputs.files[$relative] = (Get-FileHash -LiteralPath (Join-Path $root $relative)).Hash
    }
    $legacyShaderCache = Join-Path $cache 'shaders/shareable'
    $legacyFiles = @('4D5309C9.xsh', '4D5309C9.rtv.d3d12.xpso')
    $seedLegacyCache = @($legacyFiles | Where-Object {
        Test-Path -LiteralPath (Join-Path $legacyShaderCache $_) -PathType Leaf
    }).Count -eq $legacyFiles.Count
    if ($seedLegacyCache) {
        foreach ($name in $legacyFiles) {
            $inputs.files["legacy/$name"] = (Get-FileHash -LiteralPath (Join-Path $legacyShaderCache $name)).Hash
        }
    }
    # Pack misses the game recorded (title-generated shaders the preparation
    # route does not reach): a new one prepares the pack again to cover it.
    $missDirectory = Join-Path $cache 'fh1-shader-misses'
    $misses = @(if (Test-Path -LiteralPath $missDirectory) {
        Get-ChildItem -LiteralPath $missDirectory -Filter '*.bin' | Sort-Object Name
    })
    foreach ($miss in $misses) { $inputs.files["miss/$($miss.Name)"] = (Get-FileHash -LiteralPath $miss.FullName).Hash }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $key = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(
        ($inputs | ConvertTo-Json -Depth 6 -Compress))))).Replace('-', '') }
    finally { $sha.Dispose() }
    $activePath = Join-Path $cache 'fh1-artifacts.json'
    if (Test-ArtifactSet (Read-Receipt $activePath) $cache $key) {
        Write-PinyonEvent shaders 100 'Graphics are ready.' -JsonEvents:$JsonEvents
        return
    }

    Write-PinyonEvent shaders 0 "Preparing graphics for ${scale}x. This only runs when needed." -JsonEvents:$JsonEvents
    $indexPath = Join-Path $root ".local/native-renderer/managed/$key.json"
    $ready = Read-Receipt $indexPath
    $sourceCache = $null
    if ($null -ne $ready) {
        try { $sourceCache = Resolve-PinyonLocalPath -RelativePath $ready.source_cache }
        catch { $sourceCache = $null }
    }
    if (-not $sourceCache -or -not (Test-ArtifactSet $ready $sourceCache $key)) {
        $relativeWork = '.local/native-renderer/managed/run-' + [Guid]::NewGuid().ToString('N')
        $work = Resolve-PinyonLocalPath -RelativePath $relativeWork
        & (Join-Path $PSScriptRoot 'produce-fh1-artifacts.ps1') -WorkRoot $relativeWork `
            -RenderTestScript (Join-Path $root 'config/render-tests/fh1-shader-preparation.fh1test') `
            -GameRoot $GameRoot -BuildDirectory $BuildDirectory -RuntimeConfig $config -Scale $scale -Hidden -IncludeOpeningMovies `
            -AllowPipelineDiscovery -SeedShaderCacheRoot $(if ($seedLegacyCache) { $legacyShaderCache }) `
            -ShaderMissDir $(if ($misses.Count) { $missDirectory }) -JsonEvents:$JsonEvents |
            ForEach-Object { if ($_ -is [string] -and $_.StartsWith('::pinyon::')) { Write-Output $_ } }
        $report = Read-Receipt (Join-Path $work 'production.json')
        if ($null -eq $report -or $report.result -ne 'shaders-validated') { throw 'Graphics preparation did not finish validation.' }
        $sourceCache = Join-Path $work 'strict-state/cache'
        $packs = @(Get-ChildItem -LiteralPath (Join-Path $sourceCache 'shaders/shareable') -Filter '*.pnsp')
        if ($packs.Count -ne 1) { throw 'Graphics preparation did not produce exactly one shader pack.' }
        $files = @('fh1-gpu-prewarm-v3.txt', 'fh1-native-shaders-v2.bin', 'fh1-native-pipelines-v1.bin',
            ('shaders/shareable/' + $packs[0].Name))
        $ready = [ordered]@{ schema_version = 1; key = $key; source_cache = "$relativeWork/strict-state/cache"; files = @(
            foreach ($relative in $files) { [ordered]@{ path = $relative; sha256 = (Get-FileHash -LiteralPath (Join-Path $sourceCache $relative)).Hash } }
        ) }
        Write-AtomicJson $indexPath $ready
    }

    # Commit the receipt last. An interrupted activation is never accepted as
    # ready; the next launch restages the already validated set automatically.
    foreach ($file in $ready.files) {
        $source = Join-Path $sourceCache $file.path
        $destination = Join-Path $cache $file.path
        [void][IO.Directory]::CreateDirectory((Split-Path $destination -Parent))
        $temporary = "$destination.$([Guid]::NewGuid().ToString('N')).tmp"
        [IO.File]::Copy($source, $temporary)
        if ((Get-FileHash -LiteralPath $temporary).Hash -ne $file.sha256) { throw 'Prepared graphics failed the integrity check.' }
        Move-IntoPlace $temporary $destination
    }
    if (-not (Test-ArtifactSet $ready $cache $key)) { throw 'Graphics activation failed validation.' }
    Write-AtomicJson $activePath $ready
    Write-PinyonEvent shaders 100 'Graphics are ready.' -JsonEvents:$JsonEvents
} finally { $lock.Dispose() }
