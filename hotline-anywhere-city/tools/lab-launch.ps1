# Lab only: restart GTA 2 outside the app container (via Explorer + a shortcut that sets the working folder).
param([string]$Game = "D:\STEAM INSTALL DIRECTORY\steamapps\common\Grand Theft Auto 2", [int]$Wait = 12)
Get-Process gta2 -EA 0 | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 800
$lnk = Join-Path $env:TEMP "hac-gta2.lnk"
$ws = New-Object -ComObject WScript.Shell
$s = $ws.CreateShortcut($lnk); $s.TargetPath = "$Game\gta2.exe"; $s.WorkingDirectory = $Game; $s.Save()
Start-Process explorer.exe -ArgumentList "`"$lnk`""
Start-Sleep $Wait
$p = Get-Process gta2 -EA 0
if ($p) { "running pid=$($p.Id)" } else { "not running" }
