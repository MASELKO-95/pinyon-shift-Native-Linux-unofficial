[CmdletBinding()]
param(
    [string]$BuildDirectory = (Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..')).Path 'out/build/win-amd64-relwithdebinfo'),
    [switch]$Json
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'release-common.ps1')

$root = Get-PinyonRepoRoot
$toolchain = Get-PinyonReleaseToolchain
$llvmBin = Join-Path $root $toolchain.llvm.install_path 'bin'
$readobj = Join-Path $llvmBin 'llvm-readobj.exe'
$pdbutil = Join-Path $llvmBin 'llvm-pdbutil.exe'
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$modules = @(
    @{ binary = 'pinyon_shift.exe'; pdb = 'pinyon_shift.pdb' },
    @{ binary = 'pinyon_shift_SpeechFacade_default.dll'; pdb = 'pinyon_shift_SpeechFacade_default.pdb' },
    @{ binary = 'pinyon_shift_XMediaFacade_default.dll'; pdb = 'pinyon_shift_XMediaFacade_default.pdb' },
    @{ binary = 'rexruntimerd.dll'; pdb = 'rexglue-artifacts/rexruntimerd.pdb' },
    @{ binary = 'rexgpu-fh1rd.dll'; pdb = 'rexglue-artifacts/rexgpu-fh1rd.pdb' }
)

$results = foreach ($module in $modules) {
    $binary = Join-Path $build $module.binary
    $pdb = Join-Path $build $module.pdb
    if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
        throw "Profile binary is missing: $binary"
    }
    if (-not (Test-Path -LiteralPath $pdb -PathType Leaf)) {
        throw "Profile PDB is missing: $pdb"
    }
    $debugDirectory = & $readobj --coff-debug-directory $binary 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $debugDirectory -notmatch '(?s)PDBGUID: (\{[^}]+\}).*?PDBAge: (\d+)') {
        throw "Binary has no CodeView PDB identity: $binary"
    }
    $binaryGuid = $Matches[1]
    $binaryAge = $Matches[2]
    $summary = & $pdbutil dump -summary $pdb 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $summary -notmatch 'Has Publics: true') {
        throw "PDB has no usable public symbols: $pdb"
    }
    if ($summary -notmatch '(?s)Age: (\d+).*?GUID: (\{[^}]+\})' -or
        $Matches[1] -ne $binaryAge -or $Matches[2] -ne $binaryGuid) {
        throw "PDB identity does not match its binary: $pdb (binary $binaryGuid/$binaryAge; PDB $($Matches[2])/$($Matches[1]))"
    }
    [ordered]@{
        binary = [IO.Path]::GetRelativePath($root, $binary).Replace('\', '/')
        pdb = [IO.Path]::GetRelativePath($root, $pdb).Replace('\', '/')
        binary_sha256 = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
        pdb_sha256 = (Get-FileHash -LiteralPath $pdb -Algorithm SHA256).Hash
    }
}

$result = [ordered]@{
    schema = 'pinyon-shift.profile-symbols.v1'
    configuration = 'RelWithDebInfo'
    modules = @($results)
}
if ($Json) { $result | ConvertTo-Json -Depth 4 } else { $result }
