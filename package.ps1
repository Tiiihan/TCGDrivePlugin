<#
.SYNOPSIS
  Build the Total Commander plugin distribution ZIP.

.DESCRIPTION
  Stages TCGDrivePlugin.wfx64, pluginst.inf, install.bat and every required
  runtime DLL (Qt, libcurl, OpenSSL, Qt platform plugin) into a flat folder
  and zips it. The resulting ZIP is what end users open inside Total
  Commander — TC reads pluginst.inf and installs the plugin in one click.

.PARAMETER Config
  MSBuild configuration name. Defaults to Release.

.PARAMETER Version
  Version string baked into the ZIP filename.

.PARAMETER OutputDir
  Where to put the ZIP, relative to the project root. Defaults to "distributed_zip".

.EXAMPLE
  pwsh .\package.ps1
  pwsh .\package.ps1 -Config Debug -Version 1.0.1-rc1
#>
param(
    [string]$Config    = 'Release',
    [string]$Version   = '1.0.0',
    [string]$OutputDir = 'distributed_zip'
)

$ErrorActionPreference = 'Stop'
$root        = Split-Path -Parent $MyInvocation.MyCommand.Definition
$releaseDir  = Join-Path $root "x64\$Config"
$wfx         = Join-Path $releaseDir 'TCGDrivePlugin.wfx64'

if (-not (Test-Path $wfx)) {
    throw "TCGDrivePlugin.wfx64 not found in $releaseDir. " +
          "Build the $Config|x64 configuration first."
}

# --- staging directory ------------------------------------------------------
$stage = Join-Path $env:TEMP "tcgdrive-pkg-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $stage -Force | Out-Null
$platformsStage = Join-Path $stage 'platforms'
New-Item -ItemType Directory -Path $platformsStage -Force | Out-Null

# Helper: copy if the source exists, otherwise look in a fallback location.
function Copy-IfExists {
    param(
        [string]   $Primary,
        [string]   $Fallback = $null,
        [string]   $Dest,
        [string]   $Label
    )
    if (Test-Path $Primary) {
        Copy-Item $Primary $Dest -Force
        return $true
    }
    if ($Fallback -and (Test-Path $Fallback)) {
        Copy-Item $Fallback $Dest -Force
        return $true
    }
    Write-Warning "Missing $Label (looked in `"$Primary`"$(if ($Fallback) { ` and `"$Fallback`" }))"
    return $false
}

try {
    # ----- core files -------------------------------------------------------
    Copy-Item $wfx                              $stage -Force
    Copy-Item (Join-Path $root 'pluginst.inf')  $stage -Force

    # Ship install.bat as a fallback for users who prefer not to open the
    # ZIP in Total Commander (or whose TC is broken/missing).
    if (Test-Path (Join-Path $root 'install.bat')) {
        Copy-Item (Join-Path $root 'install.bat') $stage -Force
    }

    # ----- Qt runtime -------------------------------------------------------
    foreach ($dll in 'Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll') {
        Copy-IfExists -Primary (Join-Path $releaseDir $dll) `
                      -Dest    $stage `
                      -Label   $dll | Out-Null
    }

    Copy-IfExists -Primary (Join-Path $releaseDir 'platforms\qwindows.dll') `
                  -Dest    $platformsStage `
                  -Label   'platforms\qwindows.dll' | Out-Null

    # Qt 6 on Windows usually links ICU statically; copy any that the
    # particular Qt build did require (icudt*.dll, icuuc.dll, icuin.dll).
    Get-ChildItem -Path $releaseDir -Filter 'icu*.dll' -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item $_.FullName $stage -Force }

    # ----- libcurl + zlib ---------------------------------------------------
    foreach ($dll in 'libcurl.dll','z.dll') {
        Copy-IfExists -Primary  (Join-Path $releaseDir                    $dll) `
                      -Fallback (Join-Path $root "third_party\curl\bin\$dll") `
                      -Dest     $stage `
                      -Label    $dll | Out-Null
    }

    # ----- OpenSSL ----------------------------------------------------------
    foreach ($dll in 'libssl-3-x64.dll','libcrypto-3-x64.dll') {
        Copy-IfExists -Primary  (Join-Path $releaseDir                       $dll) `
                      -Fallback (Join-Path $root "third_party\openssl\bin\$dll") `
                      -Dest     $stage `
                      -Label    $dll | Out-Null
    }

    # ----- end-user README inside the ZIP -----------------------------------
    $readme = @"
TCGDrivePlugin $Version
=======================

Google Drive integration plugin for Total Commander.

INSTALLATION
------------
Easiest:
  Open this ZIP inside Total Commander itself (just enter the .zip in a
  TC panel as you would a folder). TC detects pluginst.inf and offers a
  one-click install — accept the prompt and the plugin lands in
  <TC>\plugins\wfx\TCGDrivePlugin\ and is registered automatically.

Alternative:
  Extract the ZIP, close Total Commander, and run install.bat.

REQUIREMENTS
------------
* Windows 10 or 11 (64-bit)
* Total Commander x64 (version 9.0 or newer)
* Microsoft Edge WebView2 Runtime
    Pre-installed on Windows 11 and on most up-to-date Windows 10 systems.
    If missing: install "Microsoft Edge WebView2 Evergreen Bootstrapper"
    from Microsoft.
* Microsoft Visual C++ 2015-2022 x64 Redistributable
    Install vc_redist.x64.exe from Microsoft if the plugin fails to load.

FIRST USE
---------
Open Network Neighborhood -> Google Drive in Total Commander.
Click [Sign in to Google Drive]. A secure Google sign-in window opens
inside the plugin (no external browser). After you complete the sign-in,
the panel refreshes to your Drive files.

DIAGNOSTICS
-----------
Log file: %APPDATA%\TCGDrivePlugin\plugin.log
Config:   %APPDATA%\TCGDrivePlugin\config.ini
Tokens:   %APPDATA%\TCGDrivePlugin\tokens.dat (DPAPI-encrypted, per user)
"@
    Set-Content -Path (Join-Path $stage 'README.txt') -Value $readme -Encoding utf8

    # ----- zip --------------------------------------------------------------
    $outDir  = Join-Path $root $OutputDir
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
    $zipPath = Join-Path $outDir "TCGDrivePlugin-$Version.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

    Compress-Archive -Path "$stage\*" -DestinationPath $zipPath -CompressionLevel Optimal

    Write-Host ""
    Write-Host "Package created: $zipPath" -ForegroundColor Green
    Write-Host "Size:            $([math]::Round((Get-Item $zipPath).Length / 1MB, 2)) MB"
    Write-Host ""
    Write-Host "Contents:"
    Get-ChildItem -Path $stage -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($stage.Length + 1)
        Write-Host ("  {0,-40} {1,10:N0} bytes" -f $rel, $_.Length)
    }
}
finally {
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}
