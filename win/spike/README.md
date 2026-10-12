# minSolo Windows spike

A console CLI that answers the open questions in `../../plans/app-mute-plan-windows.md`,
built and run on the Windows box (`C:\qcb-lab\minsolo\`).

```powershell
# in a VS 18 developer shell (Launch-VsDevShell.ps1 -Arch amd64)
cl /nologo /EHsc /W3 /std:c++17 /DUNICODE /D_UNICODE appmute-spike.cpp ole32.lib
cl /nologo /EHsc /W3 /DUNICODE /D_UNICODE player.cpp winmm.lib     # test source; copy as playerb.exe, playerc.exe

appmute-spike list                      # every session on every active render endpoint
appmute-spike mute chrome.exe           # ISimpleAudioVolume::SetMute on every session owned by chrome.exe
appmute-spike unmute chrome.exe
appmute-spike solo player.exe [--system]  # mute everything else (system sounds too with --system); hold; restore
appmute-spike hold player.exe           # mute it, follow new sessions, restore on Ctrl-C
```

`SPIKE_SECONDS=10` ends a hold without Ctrl-C (for scripted runs).
`player.ps1` loops a system WAV from powershell.exe; `player.exe` does the same from a
named exe so solo has distinct targets.

## Findings (2026-10-12, Windows 11 Pro 26200, 4 render endpoints)

| Question | Result |
|---|---|
| Mute by exe | **Works.** `SetMute(TRUE)` on each owned session; `list` reads back `muted YES`, which is what the Volume Mixer shows. |
| Solo | **Works.** Other apps' sessions muted, target left alone (and unmuted if it was muted). |
| New session during solo | **Caught.** `IAudioSessionNotification::OnSessionCreated` fired within the hold and the new exe was muted at once. The callback fired twice for one session in one run; de-dupe on `GetSessionInstanceIdentifier`. |
| Restore | **Works.** Original mute states restored on exit; nothing left muted. |
| Session lifetime | Sessions of exited players disappeared from the enumerator within 2 s (no lingering `expired` rows here). Still filter on state. |
| System sounds | Separate pid-0 session per endpoint; `IsSystemSoundsSession()` identifies it; solo leaves it alone unless `--system`. |
| Multiple endpoints | Sessions live on the endpoint they play through (all on the default here). Always walk every active endpoint. |
| MTA / SSH | Registering notifications from an MTA thread worked. Notably, a process in the SSH logon session enumerates and mutes the desktop session's audio sessions: the session manager is system-wide per endpoint. |
| Grouping | Exe basename (case-insensitive) was enough here; multi-process apps need the same rule across pids. |

Zero added latency holds by construction: muting happens inside the audio engine's mix.
