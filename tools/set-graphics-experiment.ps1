[CmdletBinding()]
param(
    [ValidateSet('Get', 'Apply', 'Reset', 'Restore')]
    [string]$Action = 'Get',
    [ValidateSet(4, 8, 16)]
    [int]$Anisotropy = 4,
    [ValidateSet('none', 'fxaa', 'fxaa_extreme')]
    [string]$PostEffect = 'none',
    [ValidateSet(1, 2, 3)]
    [int]$ResolutionScale = 1,
    [ValidateSet('custom', 'shipping_1x', 'experimental_2x', 'experimental_3x')]
    [string]$Preset = 'custom',
    [ValidateSet('legacy', 'fake', 'fast', 'strict')]
    [string]$OcclusionQuery = 'legacy',
    [ValidateSet('report_layout', 'pairwise_sentinel', 'relaxed_sentinel')]
    [string]$ZpdEndPolicy = 'report_layout',
    [ValidateSet('none', 'pairwise_sentinel', 'relaxed_sentinel')]
    [string]$ZpdEndFallback = 'pairwise_sentinel',
    [ValidateSet(0, 30, 60, 120, 240)]
    [int]$PresentationFps = 0,
    [ValidateRange(0, 240)]
    [int]$RenderFps = 0,
    [ValidateSet('true', 'false')]
    [string]$DisableMotionBlur = 'false',
    [ValidateSet('true', 'false')]
    [string]$DisableDepthOfField = 'false',
    [string]$StateRoot,
    [switch]$Json
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$resolvedStateRoot = if ($StateRoot) {
    [IO.Path]::GetFullPath($StateRoot)
} else {
    Join-Path $repoRoot '.local/preview'
}
$configDirectory = Join-Path $resolvedStateRoot 'config'
$configPath = Join-Path $configDirectory 'pinyon_shift.toml'
$backupDirectory = Join-Path $configDirectory 'backups'

function Get-DefaultConfigText {
    @'
# Pinyon Shift host configuration.
# Schema 24 retires the renderer choice; the native renderer is the only one.
pinyon_shift_config_schema = 24
input_backend = "sdl"
hid_mappings_file = "gamecontrollerdb.txt"
mnk_mode = true
keybind_a = "LMB,Space"
keybind_start = "Return"
d3d12_allow_variable_refresh_rate_and_tearing = false
vsync = true
host_present_fps_limit = 0
host_present_sleep_spin = true
pinyon_shift_stabilize_vehicle_presentation = false
pinyon_shift_skip_opening_movies = false
pinyon_shift_fh1_render_fps_limit = 0
pinyon_shift_fh1_source_presentation = true
xma_relaxed_padding_admission = false
anisotropic_override = 3
swap_post_effect = "none"
disable_motion_blur = false
disable_depth_of_field = false
draw_resolution_scale_x = 1
draw_resolution_scale_y = 1
clear_memory_page_state = true
occlusion_query = "legacy"
zpd_end_policy = "report_layout"
zpd_end_fallback = "pairwise_sentinel"
'@
}

# Settings a config file may still carry from an earlier release. Apply writes
# the current schema, so the game's own migration never sees the file again:
# drop every setting that migration retires (src/pinyon_shift_app.cpp).
$retiredSettings = @(
    'pinyon_shift_fh1_guest_vblank_hz',
    'pinyon_shift_native_renderer_texture_bridge',
    'pinyon_shift_native_renderer',
    'pinyon_shift_native_renderer_sky_horizon_suppression',
    'pinyon_shift_native_renderer_census',
    'pinyon_shift_fh1_native_v4',
    'readback_resolve',
    'readback_resolve_half_pixel_offset',
    'readback_memexport',
    'readback_memexport_fast',
    'fh1_renderer',
    'fh1_native_shadow',
    'fh1_native_shadow_dump_dir',
    'fh1_native_shadow_dump_frames',
    'fh1_native_shadow_verify',
    'fh1_native_shadow_verify_draws',
    'fh1_discovery_sampling',
    'fh1_owned_depth_clear',
    'fh1_owned_depth_tile_clear',
    'fh1_native_reflection_mips',
    'fh1_mip_decode_probe',
    'fh1_native_ui_boundary_probe',
    'fh1_glow_probe',
    'fh1_recycle_geometry_buffers',
    'fh1_contain_geometry_windows',
    'fh1_cache_geometry_rejections',
    'fh1_geometry_cache_mb',
    'native_stencil_value_output',
    'native_stencil_value_output_d3d12_intel',
    'pinyon_shift_native_race',
    'pinyon_shift_native_race_capture_start_frame',
    'pinyon_shift_native_ui_live',
    'pinyon_shift_native_ui_replay_source_frame',
    'pinyon_shift_native_ui_scene_probe',
    'pinyon_shift_native_ui_shadow_start_frame',
    'pinyon_shift_native_ui_clear_probe',
    'pinyon_shift_native_output_clear_probe',
    'pinyon_shift_native_scene_clear_probe',
    'pinyon_shift_native_scene_triangle_probe',
    'pinyon_shift_native_small_target_probe',
    'pinyon_shift_native_track_probe',
    'pinyon_shift_native_ordered_live_probe',
    'pinyon_shift_fh1_clear_producer_trace',
    'pinyon_shift_fh1_scene_dump',
    'pinyon_shift_snr01_trace_following_frame',
    'pinyon_shift_snr01_trace_resident_packet_writers',
    'pinyon_shift_snr01_trace_source_frame',
    'pinyon_shift_snr01_watch_packet_pages',
    'pinyon_shift_snr02_item_payload_probe',
    'pinyon_shift_snr02_trace_first_rebuild_after_frame',
    'pinyon_shift_snr02_trace_view_call',
    'pinyon_shift_snr02_track_payload_probe',
    'pinyon_shift_snr03_probe_following_frame',
    'pinyon_shift_snr03_probe_frame',
    'pinyon_shift_snr04_live_continuous',
    'pinyon_shift_snr04_live_handoff',
    'pinyon_shift_snr04_live_source_frame',
    'pinyon_shift_snr04_live_worker',
    'pinyon_shift_snr_m02_trace_source_frame'
)

function New-ConfigBackup {
    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) { return $null }
    [void](New-Item -ItemType Directory -Force -Path $backupDirectory)
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $destination = Join-Path $backupDirectory "pinyon_shift-$stamp.toml"
    Copy-Item -LiteralPath $configPath -Destination $destination
    $destination
}

function Get-SchemaVersion([string]$Text) {
    $match = [regex]::Match($Text,
        '(?m)^\s*pinyon_shift_config_schema\s*=\s*(?<value>[0-9]+)\s*(?:#.*)?$')
    if (-not $match.Success) { throw 'The host configuration has no schema version.' }
    [int]$match.Groups['value'].Value
}

function Set-TomlValue([string]$Text, [string]$Name, [string]$Value) {
    $pattern = '(?m)^\s*' + [regex]::Escape($Name) + '\s*=.*$'
    $replacement = "$Name = $Value"
    if ([regex]::IsMatch($Text, $pattern)) {
        return [regex]::Replace($Text, $pattern, $replacement, 1)
    }
    $trimmed = $Text.TrimEnd("`r", "`n")
    "$trimmed`r`n$replacement`r`n"
}

function Remove-TomlValue([string]$Text, [string]$Name) {
    [regex]::Replace(
        $Text,
        '(?m)^\s*' + [regex]::Escape($Name) + '\s*=.*(?:\r?\n|$)',
        '')
}

function Get-TomlValue([string]$Text, [string]$Name, [string]$Default) {
    $pattern = '(?m)^\s*' + [regex]::Escape($Name) + '\s*=\s*(?<value>[^#\r\n]+)'
    $match = [regex]::Match($Text, $pattern)
    if ($match.Success) { return $match.Groups['value'].Value.Trim().Trim('"') }
    $Default
}

function Write-Config([string]$Text) {
    [void](New-Item -ItemType Directory -Force -Path $configDirectory)
    $temporary = "$configPath.tmp"
    try {
        [IO.File]::WriteAllText($temporary, $Text.TrimEnd("`r", "`n") + [Environment]::NewLine,
            [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $temporary -Destination $configPath -Force
    }
    finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
    }
}

function Get-SettingsResult([string]$Text, [string]$BackupPath, [string]$Operation) {
    $override = [int](Get-TomlValue $Text 'anisotropic_override' '3')
    $anisotropyValue = switch ($override) { 3 { 4 } 4 { 8 } 5 { 16 } default { 4 } }
    $resolutionScale = [int](Get-TomlValue $Text 'draw_resolution_scale_x' '1')
    $clearPageState = (Get-TomlValue $Text 'clear_memory_page_state' 'true') -eq 'true'
    $vsyncEnabled = (Get-TomlValue $Text 'vsync' 'true') -eq 'true'
    $presentationFps = [int](Get-TomlValue $Text 'host_present_fps_limit' '0')
    $renderFps = [int](Get-TomlValue $Text 'pinyon_shift_fh1_render_fps_limit' '0')
    $presetName = if ($clearPageState -and $resolutionScale -eq 2) {
        'experimental_2x'
    } elseif ($clearPageState -and $resolutionScale -eq 3) {
        'experimental_3x'
    } elseif ($clearPageState -and $resolutionScale -eq 1 -and $vsyncEnabled) {
        'shipping_1x'
    } else {
        'custom'
    }
    [ordered]@{
        schema = 'pinyon-shift.graphics-settings.v2'
        operation = $Operation.ToLowerInvariant()
        config_path = $configPath
        backup_path = $BackupPath
        settings = [ordered]@{
            anisotropy = $anisotropyValue
            post_effect = Get-TomlValue $Text 'swap_post_effect' 'none'
            disable_motion_blur = (Get-TomlValue $Text 'disable_motion_blur' 'false') -eq 'true'
            disable_depth_of_field = (Get-TomlValue $Text 'disable_depth_of_field' 'false') -eq 'true'
            preset = $presetName
            resolution_scale = $resolutionScale
            clear_memory_page_state = $clearPageState
            vsync = $vsyncEnabled
            host_present_fps_limit = $presentationFps
            fh1_render_fps_limit = $renderFps
            host_present_sleep_spin =
                (Get-TomlValue $Text 'host_present_sleep_spin' 'true') -eq 'true'
            occlusion_query = Get-TomlValue $Text 'occlusion_query' 'legacy'
            zpd_end_policy = Get-TomlValue $Text 'zpd_end_policy' 'report_layout'
            zpd_end_fallback = Get-TomlValue $Text 'zpd_end_fallback' 'pairwise_sentinel'
        }
        restart_required = $Operation -ne 'Get'
    }
}

[void](New-Item -ItemType Directory -Force -Path $configDirectory)
$backup = $null
switch ($Action) {
    'Get' {
        $text = if (Test-Path -LiteralPath $configPath -PathType Leaf) {
            Get-Content -LiteralPath $configPath -Raw
        } else { Get-DefaultConfigText }
        $schema = Get-SchemaVersion $text
        if ($schema -lt 1 -or $schema -gt 24) { throw "Unsupported host configuration schema: $schema" }
    }
    'Reset' {
        $backup = New-ConfigBackup
        $text = Get-DefaultConfigText
        Write-Config $text
    }
    'Restore' {
        if (-not (Test-Path -LiteralPath $backupDirectory -PathType Container)) {
            throw 'No runtime-settings backup is available.'
        }
        $source = Get-ChildItem -LiteralPath $backupDirectory -Filter 'pinyon_shift-*.toml' -File |
            Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
        if ($null -eq $source) { throw 'No runtime-settings backup is available.' }
        $backup = New-ConfigBackup
        $text = Get-Content -LiteralPath $source.FullName -Raw
        $schema = Get-SchemaVersion $text
        if ($schema -lt 1 -or $schema -gt 24) { throw "Backup uses unsupported schema: $schema" }
        Write-Config $text
    }
    'Apply' {
        $text = if (Test-Path -LiteralPath $configPath -PathType Leaf) {
            Get-Content -LiteralPath $configPath -Raw
        } else { Get-DefaultConfigText }
        $schema = Get-SchemaVersion $text
        if ($schema -lt 1 -or $schema -gt 24) { throw "Unsupported host configuration schema: $schema" }
        $backup = New-ConfigBackup
        foreach ($retired in $retiredSettings) {
            $text = Remove-TomlValue $text $retired
        }
        $text = Set-TomlValue $text 'pinyon_shift_config_schema' '24'
        $text = Set-TomlValue $text 'pinyon_shift_fh1_render_fps_limit' '0'
        $text = Set-TomlValue $text 'pinyon_shift_fh1_source_presentation' 'true'
        if (-not [regex]::IsMatch($text, '(?m)^\s*xma_relaxed_padding_admission\s*=')) {
            $text = Set-TomlValue $text 'xma_relaxed_padding_admission' 'false'
        }
        if (-not [regex]::IsMatch($text, '(?m)^\s*occlusion_query\s*=')) {
            $text = Set-TomlValue $text 'occlusion_query' '"legacy"'
        }
        $effectiveResolution = switch ($Preset) {
            'shipping_1x' { 1 }
            'experimental_2x' { 2 }
            'experimental_3x' { 3 }
            default { $ResolutionScale }
        }
        $override = switch ($Anisotropy) { 4 { 3 } 8 { 4 } 16 { 5 } }
        $text = Set-TomlValue $text 'anisotropic_override' ([string]$override)
        $text = Set-TomlValue $text 'swap_post_effect' ('"' + $PostEffect + '"')
        $text = Set-TomlValue $text 'disable_motion_blur' $DisableMotionBlur
        $text = Set-TomlValue $text 'disable_depth_of_field' $DisableDepthOfField
        $text = Set-TomlValue $text 'draw_resolution_scale_x' ([string]$effectiveResolution)
        $text = Set-TomlValue $text 'draw_resolution_scale_y' ([string]$effectiveResolution)
        $text = Set-TomlValue $text 'vsync' 'true'
        $text = Set-TomlValue $text 'host_present_fps_limit' ([string]$PresentationFps)
        $text = Set-TomlValue $text 'pinyon_shift_fh1_render_fps_limit' ([string]$RenderFps)
        $text = Set-TomlValue $text 'host_present_sleep_spin' 'true'
        $text = Set-TomlValue $text 'clear_memory_page_state' 'true'
        $text = Set-TomlValue $text 'occlusion_query' ('"' + $OcclusionQuery + '"')
        $text = Set-TomlValue $text 'zpd_end_policy' ('"' + $ZpdEndPolicy + '"')
        $text = Set-TomlValue $text 'zpd_end_fallback' ('"' + $ZpdEndFallback + '"')
        Write-Config $text
    }
}

$result = Get-SettingsResult $text $backup $Action
if ($Json) { $result | ConvertTo-Json -Depth 5 -Compress } else { [pscustomobject]$result }
