[CmdletBinding()]
param(
    # The player accepted the Android SDK license (the launcher shows it
    # first); without it, missing SDK packages stop the build with a hint.
    [switch]$AcceptAndroidLicenses,
    [int]$Parallel = 0,
    [switch]$JsonEvents
)

# Builds .local/android/pinyon-shift.apk from the game this PC already built
# (tools/pinyon.py android build), for the launcher's Build Android APK
# action. A PC without an Android SDK or JDK gets the pinned command-line
# tools and Temurin JDK from config/android-toolchain.json under
# .local/toolchain; nothing outside the install folder changes.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $false
}

. (Join-Path $PSScriptRoot 'release-common.ps1')
$root = Get-PinyonRepoRoot
$logs = Resolve-PinyonLocalPath -RelativePath '.local/logs'
[void](New-Item -ItemType Directory -Force -Path $logs)
$errorPath = Join-Path $logs 'android-error.json'
if (Test-Path -LiteralPath $errorPath) { Remove-Item -LiteralPath $errorPath -Force }

function Find-PinyonAndroidSdk {
    param([Parameter(Mandatory)] [string]$LocalSdk)
    $candidates = @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT)
    if ($env:LOCALAPPDATA) { $candidates += Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
    $candidates += Join-Path $HOME 'Android\Sdk'
    $candidates += $LocalSdk
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate 'platform-tools'))) {
            return $candidate
        }
    }
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate 'cmdline-tools\latest\bin'))) {
            return $candidate
        }
    }
    $null
}

try {
    $android = Get-Content -LiteralPath (Join-Path $root 'config/android-toolchain.json') -Raw |
        ConvertFrom-Json
    $bootstrap = $android.bootstrap

    Write-PinyonEvent android 2 'Checking the PC build the Android package is made from.' -JsonEvents:$JsonEvents
    if (-not (Test-Path -LiteralPath (Join-Path $root '.local/generated/default/sources.cmake') -PathType Leaf)) {
        throw 'The game has not been built on this PC yet. Build and play it once, then build the Android package.'
    }
    $python = Get-PinyonPython
    $pinyon = Join-Path $PSScriptRoot 'pinyon.py'
    $env:PINYON_REXSDK_DIR = Resolve-PinyonRexGlueRoot

    Write-PinyonEvent android 5 'Checking the Android SDK, NDK and JDK.' -JsonEvents:$JsonEvents
    $doctorLog = Join-Path $logs 'android-doctor.log'
    Invoke-PinyonLoggedCommand -FilePath $python -Arguments @($pinyon, 'android', 'doctor') `
        -LogPath $doctorLog | Out-Host
    if ($LASTEXITCODE -ne 0) {
        # A JDK for sdkmanager, javac and keytool.
        $localJdk = Resolve-PinyonLocalPath -RelativePath $bootstrap.jdk.install_path
        $haveJdk = ($env:JAVA_HOME -and (Test-Path -LiteralPath (Join-Path $env:JAVA_HOME 'bin\javac.exe'))) -or
            (Test-Path -LiteralPath (Join-Path $localJdk 'bin\javac.exe'))
        if (-not $haveJdk -and -not $env:JAVA_HOME -and (Get-Command javac.exe -ErrorAction SilentlyContinue)) {
            $haveJdk = $true
        }
        if (-not $haveJdk) {
            Write-PinyonEvent android 8 "Downloading the Temurin JDK $($bootstrap.jdk.version) (about 190 MB)." -JsonEvents:$JsonEvents
            $archive = Resolve-PinyonLocalPath -RelativePath ".local/downloads/$(Split-Path $bootstrap.jdk.url -Leaf)"
            Invoke-PinyonDownload -Uri $bootstrap.jdk.url -Destination $archive -Sha256 $bootstrap.jdk.sha256
            $toolchain = Split-Path $localJdk -Parent
            Expand-Archive -LiteralPath $archive -DestinationPath $toolchain -Force
            if (-not (Test-Path -LiteralPath (Join-Path $localJdk 'bin\javac.exe'))) {
                throw "The JDK archive did not unpack to $localJdk."
            }
        }
        if (Test-Path -LiteralPath (Join-Path $localJdk 'bin\javac.exe')) {
            if (-not $env:JAVA_HOME) { $env:JAVA_HOME = $localJdk }
        }

        # sdkmanager, which installs the pinned NDK, build tools and platform.
        $localSdk = Resolve-PinyonLocalPath -RelativePath $bootstrap.android_sdk_path
        $sdk = Find-PinyonAndroidSdk -LocalSdk $localSdk
        if (-not $sdk -or -not (Test-Path -LiteralPath (Join-Path $sdk 'cmdline-tools\latest\bin\sdkmanager.bat'))) {
            $sdk = $localSdk
            $sdkmanager = Join-Path $sdk 'cmdline-tools\latest\bin\sdkmanager.bat'
            if (-not (Test-Path -LiteralPath $sdkmanager)) {
                Write-PinyonEvent android 12 'Downloading the Android SDK command-line tools (about 150 MB).' -JsonEvents:$JsonEvents
                $archive = Resolve-PinyonLocalPath -RelativePath ".local/downloads/$(Split-Path $bootstrap.cmdline_tools.url -Leaf)"
                Invoke-PinyonDownload -Uri $bootstrap.cmdline_tools.url -Destination $archive `
                    -Sha256 $bootstrap.cmdline_tools.sha256
                $unpack = Join-Path $sdk 'cmdline-tools\unpack'
                if (Test-Path -LiteralPath $unpack) { Remove-Item -LiteralPath $unpack -Recurse -Force }
                Expand-Archive -LiteralPath $archive -DestinationPath $unpack -Force
                $latest = Join-Path $sdk 'cmdline-tools\latest'
                if (Test-Path -LiteralPath $latest) { Remove-Item -LiteralPath $latest -Recurse -Force }
                Move-Item -LiteralPath (Join-Path $unpack 'cmdline-tools') -Destination $latest
                Remove-Item -LiteralPath $unpack -Recurse -Force
            }
        }
        $env:ANDROID_HOME = $sdk
        $env:ANDROID_SDK_ROOT = $sdk

        if (-not $AcceptAndroidLicenses) {
            $failure = [Exception]::new('Installing the Android NDK, build tools and platform needs the Android SDK license to be accepted.')
            $failure.Data['step'] = 'Install the Android SDK packages'
            $failure.Data['hint'] = "Accept the Android SDK license ($($bootstrap.cmdline_tools.license_url)) when the launcher asks, or run: python tools/pinyon.py android doctor --install"
            throw $failure
        }
        Write-PinyonEvent android 18 'Installing the Android NDK, build tools and platform (a few GB, once).' -JsonEvents:$JsonEvents
        Invoke-PinyonBuildCommand $python @($pinyon, 'android', 'doctor', '--install', '--accept-licenses') `
            (Join-Path $logs 'android-sdk-install.log') 'Installing the Android SDK packages failed.' `
            -Step 'Install the Android SDK packages'
    }

    if ($Parallel -le 0) { $Parallel = Get-PinyonBuildJobCount }
    Write-PinyonEvent android 30 'Compiling the game for Android. The first build takes 20 to 60 minutes.' -JsonEvents:$JsonEvents
    Invoke-PinyonBuildCommand $python @($pinyon, 'android', 'build', '--jobs', "$Parallel") `
        (Join-Path $logs 'android-build.log') 'The Android build failed.' -Step 'Build the Android package'

    $apk = Join-Path $root '.local\android\pinyon-shift.apk'
    if (-not (Test-Path -LiteralPath $apk -PathType Leaf)) {
        throw 'The Android build completed without producing pinyon-shift.apk.'
    }
    Write-PinyonEvent android 100 "Android package ready: $apk" -JsonEvents:$JsonEvents
}
catch {
    # As setup-preview.ps1 reports: plain text, a JSON record the launcher
    # reads, and an exit code.
    $failure = $_
    try {
        $errorRecord = New-PinyonFailureRecord -ErrorRecord $failure
        $errorRecord.system = Get-PinyonSystemSummary -Root $root
        [IO.File]::WriteAllText($errorPath, ($errorRecord | ConvertTo-Json -Depth 4) + [Environment]::NewLine,
            [Text.UTF8Encoding]::new($false))
        foreach ($line in (Format-PinyonFailureRecord -Record $errorRecord -ReportPath '.local\logs\android-error.json')) { [Console]::Out.WriteLine($line) }
        [Console]::Out.WriteLine("Failure details: $errorPath")
    }
    catch {
        [Console]::Out.WriteLine("Android build failed: $($failure.Exception.Message)")
        [Console]::Out.WriteLine("The failure report could not be written: $($_.Exception.Message)")
    }
    [Console]::Out.Flush()
    [Console]::Error.WriteLine("Android build failed: $($failure.Exception.Message)")
    exit 1
}
