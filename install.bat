@echo off
rem ===========================================================================
rem  TCGDrivePlugin installer (simple batch)
rem
rem  - copies TCGDrivePlugin.wfx64 (+ runtime DLLs found next to it) into
rem    Total Commander's plugin directory
rem  - registers the plugin in wincmd.ini ([FileSystemPlugins64])
rem  - creates %APPDATA%\TCGDrivePlugin and a starter config.ini
rem
rem  Run this script from the folder that contains the built
rem  TCGDrivePlugin.wfx64 (e.g. x64\Release after a Release|x64 build), with
rem  the Qt / libcurl / OpenSSL runtime DLLs copied alongside it.
rem
rem  Note on the extension: Total Commander identifies WFX plugin bitness by
rem  file extension, not by PE headers — .wfx == 32-bit, .wfx64 == 64-bit.
rem ===========================================================================
setlocal EnableDelayedExpansion

rem  Close any running Total Commander first. If TC is alive while we
rem  edit wincmd.ini, TC clobbers the file on next save with its cached
rem  in-memory copy (which doesn't include our new entry).
taskkill /F /IM TOTALCMD64.EXE >nul 2>&1
taskkill /F /IM TOTALCMD.EXE   >nul 2>&1
timeout /t 1 /nobreak >nul 2>&1

rem  Source folder: where this script lives. If the .wfx64 is not here
rem  (script run from the project root), fall back to x64\Release\.
set "SRC=%~dp0"
if not exist "!SRC!TCGDrivePlugin.wfx64" (
  if exist "!SRC!x64\Release\TCGDrivePlugin.wfx64" (
    set "SRC=!SRC!x64\Release\"
  )
)
set "WFX=!SRC!TCGDrivePlugin.wfx64"
if not exist "!WFX!" (
  echo [ERROR] TCGDrivePlugin.wfx64 not found next to install.bat nor in
  echo         x64\Release\. Build the Release^|x64 configuration first.
  pause
  exit /b 1
)
echo Source folder: !SRC!

rem --- locate Total Commander -------------------------------------------------
set "TC_DIR=%COMMANDER_PATH%"
if "%TC_DIR%"=="" set "TC_DIR=%ProgramFiles%\totalcmd"
if not exist "%TC_DIR%\TOTALCMD64.EXE" if not exist "%TC_DIR%\TOTALCMD.EXE" set "TC_DIR=C:\totalcmd"
if not exist "%TC_DIR%\TOTALCMD64.EXE" if not exist "%TC_DIR%\TOTALCMD.EXE" (
  echo [ERROR] Total Commander not found in "%TC_DIR%".
  echo         Set the COMMANDER_PATH environment variable to TC's install dir
  echo         and re-run this script.
  pause
  exit /b 1
)
echo Total Commander: %TC_DIR%

rem --- locate wincmd.ini ------------------------------------------------------
set "WINCMD_INI=%COMMANDER_INI%"
if "%WINCMD_INI%"=="" set "WINCMD_INI=%APPDATA%\GHISLER\wincmd.ini"
if not exist "%WINCMD_INI%" set "WINCMD_INI=%TC_DIR%\wincmd.ini"
if not exist "%WINCMD_INI%" (
  echo [WARN] wincmd.ini not found ^(looked in %APPDATA%\GHISLER and %TC_DIR%^).
  echo        Plugin files will still be copied; add it manually via
  echo        Configuration -^> Options -^> Plugins -^> "File system plugins" -^> Add.
  set "WINCMD_INI="
) else (
  echo wincmd.ini:      %WINCMD_INI%
)

rem --- copy plugin + runtime --------------------------------------------------
set "PLUGIN_DIR=%TC_DIR%\plugins\wfx\TCGDrivePlugin"
if not exist "%PLUGIN_DIR%" mkdir "%PLUGIN_DIR%"
echo.
echo Copying plugin files to "%PLUGIN_DIR%" ...
copy /Y "%WFX%" "%PLUGIN_DIR%\" >nul

if not exist "%SRC%z.dll" if exist "%~dp0third_party\curl\bin\z.dll" (
  copy /Y "%~dp0third_party\curl\bin\z.dll" "%SRC%" >nul
)

for %%F in (libcurl.dll z.dll libssl-3-x64.dll libcrypto-3-x64.dll Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll icuuc.dll icuin.dll icudt*.dll MSVCP140.dll MSVCP140_1.dll MSVCP140_2.dll VCRUNTIME140.dll VCRUNTIME140_1.dll) do (
  if exist "%SRC%%%F" copy /Y "%SRC%%%F" "%PLUGIN_DIR%\" >nul
)
if exist "%SRC%platforms\qwindows.dll" (
  if not exist "%PLUGIN_DIR%\platforms" mkdir "%PLUGIN_DIR%\platforms"
  copy /Y "%SRC%platforms\qwindows.dll" "%PLUGIN_DIR%\platforms\" >nul
)

echo.
echo Checking packaged runtime DLLs ...
for %%F in (TCGDrivePlugin.wfx64 libcurl.dll z.dll libssl-3-x64.dll libcrypto-3-x64.dll Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll) do (
  if not exist "%PLUGIN_DIR%\%%F" echo [WARN] Missing "%PLUGIN_DIR%\%%F"
)
if not exist "%PLUGIN_DIR%\platforms\qwindows.dll" echo [WARN] Missing "%PLUGIN_DIR%\platforms\qwindows.dll"

rem --- register in wincmd.ini (PowerShell — INI editing in batch is too brittle)
rem
rem  64-bit TC stores the path in [FileSystemPlugins] and a marker in
rem  [FileSystemPlugins64]. We:
rem   1) drop any existing line ending in TCGDrivePlugin.wfx or .wfx64
rem      anywhere in the file (sweeps stale entries from earlier runs
rem      and an obsolete .wfx entry from before we switched extension);
rem   2) drop any $checksum$ line so TC won't undo our edit on startup;
rem   3) make sure both sections exist and contain the TC-compatible lines.
if not "%WINCMD_INI%"=="" (
  echo Registering plugin in [FileSystemPlugins] / [FileSystemPlugins64] of wincmd.ini ...
  powershell -NoProfile -ExecutionPolicy Bypass -Command "$ini='%WINCMD_INI%'; $name='Google Drive'; $path='%PLUGIN_DIR%\TCGDrivePlugin.wfx64'; $fsLine=$name+'='+$path; $fs64Line=$name+'=1'; if (Test-Path $ini) { $lines=@(Get-Content -LiteralPath $ini) } else { $lines=@() }; $cleaned=@(); $section=''; foreach ($l in $lines) { if ($l -match '^\s*\[(.+?)\]\s*$') { $section=$matches[1] }; if ($l -match '^\s*\$checksum\$') { continue }; if ($l -match '(?i)TCGDrivePlugin\.wfx(64)?\s*$') { continue }; if (($section -ieq 'FileSystemPlugins' -or $section -ieq 'FileSystemPlugins64') -and $l -match '^\s*Google Drive\s*=') { continue }; $cleaned += $l }; function Add-IniLine($lines,$sectionName,$line) { $out=@(); $inserted=$false; $found=$false; foreach ($l in $lines) { if ($found -and -not $inserted -and $l -match '^\s*\[.+?\]\s*$') { $out += $line; $inserted=$true }; $out += $l; if (-not $found -and $l -match ('^\s*\[' + [regex]::Escape($sectionName) + '\]\s*$')) { $found=$true } }; if ($found -and -not $inserted) { $out += $line }; if (-not $found) { if ($out.Count -gt 0 -and $out[-1] -ne '') { $out += '' }; $out += '['+$sectionName+']'; $out += $line }; return $out }; $out=Add-IniLine $cleaned 'FileSystemPlugins' $fsLine; $out=Add-IniLine $out 'FileSystemPlugins64' $fs64Line; Set-Content -LiteralPath $ini -Value $out; Write-Host '  ok'"
)

rem --- %APPDATA% directory + starter config ----------------------------------
rem  OAuth credentials are compiled into the plugin (DEFAULT_CLIENT_ID /
rem  DEFAULT_CLIENT_SECRET in config_manager.h), so config.ini only needs
rem  the optional settings. (Add client_id= / client_secret= lines yourself
rem  only if you want to use your own Google Cloud project.)
set "DATA_DIR=%APPDATA%\TCGDrivePlugin"
if not exist "%DATA_DIR%" mkdir "%DATA_DIR%"
if not exist "%DATA_DIR%\config.ini" (
  echo Creating starter config "%DATA_DIR%\config.ini" ...
  > "%DATA_DIR%\config.ini" echo [General]
  >> "%DATA_DIR%\config.ini" echo cache_ttl_seconds=60
  >> "%DATA_DIR%\config.ini" echo log_level=INFO
  >> "%DATA_DIR%\config.ini" echo language=uk
)

echo.
echo ===========================================================================
echo  Done.
echo.
echo  Restart Total Commander and open
echo      Network Neighborhood  ->  Google Drive
echo  Click [Sign in to Google Drive] — a secure sign-in window opens
echo  inside the plugin (no external browser needed).
echo ===========================================================================
pause
endlocal
