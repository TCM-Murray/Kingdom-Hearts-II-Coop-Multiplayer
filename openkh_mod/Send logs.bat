@echo off
rem KH2 Coop: zips this PC's newest logs onto the Desktop, to send to the host (TODO 6.5).
rem It sits in <OpenKH folder>\mods\kh2\KH2 Coop; the logs are next to the built DLL, <OpenKH folder>\mod\kh2\dll.
rem Newest = written in the last 12 hours (a play session), or the newest one alone. Copied first: a running game
rem keeps its log open. Nothing is deleted.
setlocal
set "KH2COOP_LOGS=%~dp0..\..\..\mod\kh2\dll"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference = 'Stop';" ^
  "$all = @(Get-ChildItem -LiteralPath $env:KH2COOP_LOGS -Filter 'kh2coop_*.log' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending);" ^
  "if (-not $all) { Write-Host ('No kh2coop_*.log found in ' + $env:KH2COOP_LOGS); exit 1 }" ^
  "$new = @($all | Where-Object { $_.LastWriteTime -gt (Get-Date).AddHours(-12) });" ^
  "if (-not $new) { $new = @($all[0]) }" ^
  "$tmp = Join-Path $env:TEMP 'kh2coop-logs';" ^
  "if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }" ^
  "New-Item -ItemType Directory $tmp | Out-Null;" ^
  "$new | Copy-Item -Destination $tmp;" ^
  "$zip = Join-Path ([Environment]::GetFolderPath('Desktop')) ('kh2coop-logs-' + (Get-Date -Format 'yyyy-MM-dd_HHmm') + '.zip');" ^
  "Compress-Archive -Path (Join-Path $tmp '*') -DestinationPath $zip -Force;" ^
  "Remove-Item $tmp -Recurse -Force;" ^
  "Write-Host ('' + $new.Count + ' log(s) zipped: ' + $zip + ' (' + [math]::Round((Get-Item $zip).Length / 1MB, 1) + ' MB)');" ^
  "explorer.exe ('/select,' + $zip); exit 0"
if errorlevel 1 echo Could not zip the logs. Send the newest kh2coop_*.log from the mod\kh2\dll folder by hand.
pause
