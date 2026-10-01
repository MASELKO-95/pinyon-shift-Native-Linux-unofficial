# Edits of the host configuration (<state>/config/pinyon_shift.toml), shared
# by the launcher's tools. The in-game settings screen follows the same rules
# in src/config/host_config.cpp; tools/tests/test_host_config.py checks that
# both produce the same bytes:
#
# - A setting is the first line whose name, after leading spaces or tabs, is
#   followed by optional spaces or tabs and '='.
# - Its value runs to a '#' or the end of the line, trimmed, with the
#   surrounding quotes of a string removed; an empty value counts as absent.
# - Setting a value replaces that line (not its line ending) with
#   `name = value`, or appends the line using the file's line ending.
# - Files are written through a temporary file and end with one line ending.
# - A backup is a copy in config/backups/pinyon_shift-<UTC time>.toml.

function Get-TomlNewline([string]$Text) {
    if ($Text.Contains("`r`n")) { "`r`n" } else { "`n" }
}

function Get-TomlValue([string]$Text, [string]$Name, [string]$Default) {
    $pattern = '(?m)^[ \t]*' + [regex]::Escape($Name) + '[ \t]*=(?<value>[^#\r\n]*)'
    $match = [regex]::Match($Text, $pattern)
    if ($match.Success) {
        $value = $match.Groups['value'].Value.Trim().Trim('"')
        if ($value.Length) { return $value }
    }
    $Default
}

function Set-TomlValue([string]$Text, [string]$Name, [string]$Value) {
    $pattern = '(?m)^[ \t]*' + [regex]::Escape($Name) + '[ \t]*=[^\r\n]*'
    $replacement = "$Name = $Value"
    $regex = [regex]::new($pattern)
    if ($regex.IsMatch($Text)) {
        return $regex.Replace($Text, $replacement.Replace('$', '$$'), 1)
    }
    $newline = Get-TomlNewline $Text
    $trimmed = $Text.TrimEnd("`r", "`n")
    if ($trimmed.Length) { "$trimmed$newline$replacement$newline" } else { "$replacement$newline" }
}

function Remove-TomlValue([string]$Text, [string]$Name) {
    [regex]::Replace(
        $Text,
        '(?m)^[ \t]*' + [regex]::Escape($Name) + '[ \t]*=[^\r\n]*(?:\r?\n|$)',
        '')
}

function Write-HostConfig([string]$Path, [string]$Text) {
    [void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path))
    $newline = Get-TomlNewline $Text
    $temporary = "$Path.tmp"
    try {
        [IO.File]::WriteAllText($temporary, $Text.TrimEnd("`r", "`n") + $newline,
            [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $temporary -Destination $Path -Force
    }
    finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
    }
}

function New-HostConfigBackup([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $directory = Join-Path (Split-Path -Parent $Path) 'backups'
    [void](New-Item -ItemType Directory -Force -Path $directory)
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $destination = Join-Path $directory "pinyon_shift-$stamp.toml"
    Copy-Item -LiteralPath $Path -Destination $destination
    $destination
}
