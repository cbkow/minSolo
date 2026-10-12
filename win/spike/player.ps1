# Loop a system WAV for N seconds so the box has a live audio session owned by powershell.exe.
param([int]$Seconds = 60)
$p = New-Object System.Media.SoundPlayer "C:\Windows\Media\Alarm01.wav"
$p.PlayLooping()
Start-Sleep -Seconds $Seconds
$p.Stop()
