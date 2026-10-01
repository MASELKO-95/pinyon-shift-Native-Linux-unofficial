[CmdletBinding()]
param(
    [string]$StateRoot,
    [string]$Output,
    [string]$PythonExe,
    [string]$RenderTestScript,
    [string]$BuildDirectory,
    [switch]$PerformanceOnly,
    [string]$GameArgumentsJson,
    [int]$CheckpointSeconds = 300
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'release-common.ps1')
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $PythonExe) { $PythonExe = Get-PinyonPython }
if (-not $StateRoot) {
    $profiles = @(Get-ChildItem -LiteralPath (Join-Path $env:LOCALAPPDATA 'PinyonShift/source') -Filter ForzaProfile -File -Recurse |
        Where-Object { $_.FullName -match '\\.local\\preview\\user\\.*\\ForzaProfile\\ForzaProfile$' } |
        Sort-Object LastWriteTimeUtc -Descending)
    if (-not $profiles.Count) { throw 'Installed preview save not found' }
    $StateRoot = $profiles[0].FullName -replace '(\\\.local\\preview)\\user\\.*$', '$1'
}
$StateRoot = (Resolve-Path -LiteralPath $StateRoot).Path
$profiles = @(Get-ChildItem -LiteralPath (Join-Path $StateRoot 'user') -Filter ForzaProfile -File -Recurse |
    Where-Object { $_.Directory.Name -eq 'ForzaProfile' })
if (-not $profiles.Count) { throw 'Forza profile missing in the selected state root' }
if (Get-Process pinyon_shift -ErrorAction SilentlyContinue) { throw 'Game already running' }
if ($CheckpointSeconds -lt 5) { throw 'Checkpoint interval must be at least 5 seconds' }
if (-not $Output) { $Output = Join-Path $repo ('.local/native-renderer/discovery/' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$Output = [IO.Path]::GetFullPath($Output)
& $PythonExe (Join-Path $PSScriptRoot 'record-fh1-discovery.py') --prepare --state-root $StateRoot --output $Output
if ($LASTEXITCODE -ne 0) { throw 'Recorder preparation failed' }
$exeRoot = if ($BuildDirectory) {
    (Resolve-Path -LiteralPath $BuildDirectory).Path
} else { Join-Path $repo 'out/build/win-amd64-release' }
$manifest = [ordered]@{
    started_utc = [DateTime]::UtcNow.ToString('o')
    state_root = $StateRoot
    render_test = if ($RenderTestScript) { (Resolve-Path -LiteralPath $RenderTestScript).Path } else { $null }
    game_arguments = if ($GameArgumentsJson) { @(ConvertFrom-Json $GameArgumentsJson) } else { @() }
    os = [Environment]::OSVersion.VersionString
    gpus = @(Get-CimInstance Win32_VideoController | ForEach-Object {
        [ordered]@{ name=$_.Name; driver_version=$_.DriverVersion; adapter_ram=$_.AdapterRAM }
    })
    files = @{}
    state_artifacts = @{}
}
foreach ($name in @('pinyon_shift.exe','rexgpu-fh1.dll','rexruntime.dll',
                    'pinyon_shift_SpeechFacade_default.dll','pinyon_shift_XMediaFacade_default.dll')) {
    $manifest.files[$name] = (Get-FileHash -LiteralPath (Join-Path $exeRoot $name)).Hash
}
$runtimeManifest = Join-Path $exeRoot 'pinyon_shift_build.json'
if (Test-Path -LiteralPath $runtimeManifest) {
    $manifest.source = Get-Content -LiteralPath $runtimeManifest -Raw | ConvertFrom-Json
}
if ($RenderTestScript) {
    $manifest.files['render_test'] = (Get-FileHash -LiteralPath $RenderTestScript).Hash
}
$cache = Join-Path $StateRoot 'cache'
if (Test-Path -LiteralPath $cache) {
    Get-ChildItem -LiteralPath $cache -File | Where-Object {
        $_.Name -like 'fh1-native-*' -or $_.Name -like 'fh1-gpu-prewarm-*' -or $_.Extension -eq '.pnsp'
    } | ForEach-Object { $manifest.state_artifacts[$_.Name] = (Get-FileHash -LiteralPath $_.FullName).Hash }
}
$config = Join-Path $StateRoot 'config/pinyon_shift.toml'
if (Test-Path -LiteralPath $config) {
    Copy-Item -LiteralPath $config -Destination (Join-Path $Output 'settings.toml')
    $manifest.files['settings.toml'] = (Get-FileHash -LiteralPath $config).Hash
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Output 'build.json')
$job = Start-Job -ArgumentList $repo,$StateRoot,$Output,$RenderTestScript,$CheckpointSeconds,$exeRoot,$PerformanceOnly.IsPresent,$GameArgumentsJson -ScriptBlock {
    param($repo,$stateRoot,$output,$renderTest,$checkpoint,$buildDirectory,$performanceOnly,$extraArgumentsJson)
    Set-Location $repo
    $options = @{
        StateRoot=$stateRoot
        BuildDirectory=$buildDirectory
        CollectFh1PassInventory=$true
        GameArguments=@('--pinyon_shift_capture_performance=true', '--perf_log_max_mb=512',
            '--log_max_file_size_mb=5', '--log_max_files=20')
        Json=$true
    }
    if ($performanceOnly) {
        $options.CollectFh1PassInventory=$false
        $options.GameArguments=@('--pinyon_shift_capture_performance=true', '--perf_log_max_mb=512')
    }
    if ($renderTest) {
        $options.RenderTestScript=$renderTest
        $options.RenderTestOutput=Join-Path $output 'smoke'
        $options.RenderTestTimeoutSeconds=180
    }
    if ($extraArgumentsJson) {
        $options.GameArguments += @(ConvertFrom-Json $extraArgumentsJson)
    }
    & ./tools/launch-preview.ps1 @options
}
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(90)
    do {
        $game = Get-Process pinyon_shift -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($game) { break }
        if ($job.State -ne 'Running' -or [DateTime]::UtcNow -ge $deadline) {
            Receive-Job $job | Out-File (Join-Path $Output 'launch.log')
            throw 'Game did not start; see launch.log'
        }
        Start-Sleep -Milliseconds 200
    } while ($true)
    Write-Output "Discovery recording: $Output"
    Write-Output 'Ctrl+Shift+F8: slowdown. Ctrl+Shift+F9: visual/timing problem. Close the game normally when done.'
    & $PythonExe (Join-Path $PSScriptRoot 'record-fh1-discovery.py') --pid $game.Id --state-root $StateRoot --output $Output *> (Join-Path $Output 'recorder.log')
    $recorderExit = $LASTEXITCODE
    # Keep the launcher alive even if the recorder fails; never interrupt the player's game.
    Receive-Job $job -Wait | Out-File (Join-Path $Output 'launch.log')
    if ($recorderExit -ne 0) { throw 'Recorder failed; see recorder.log' }
} finally {
    if ($job.State -ne 'Running') { Remove-Job $job }
}
