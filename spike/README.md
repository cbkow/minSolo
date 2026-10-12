# minSolo macOS spike

A signed CLI that answers the open questions in `../plans/app-mute-plan.md`.

```bash
./sign.sh                    # swift build -c release + codesign (Developer ID, hardened runtime, no timestamp)
B=.build/release/minsolo-spike
$B list                      # audio processes: object id, pid, bundle id, output running
$B taps                      # taps on the system (name, mute behavior, process object ids)
$B mute <bundleID|pid> [--io] [--when-tapped|--unmuted] [--watch] [--public]
$B solo <bundleID|pid> [--io]
$B destroy <tapID>|--all     # remove orphaned taps
```

Bundle IDs match by prefix, so `com.google.Chrome` also covers `com.google.Chrome.helper`.
Ctrl-C destroys the tap (and the aggregate device if `--io`).

## Findings so far (2026-10-12)

| Question | Result |
|---|---|
| Does `CATapMuted` work with only a tap? | **No.** QCView stayed audible under a tap-only mute and went silent with `--io` (aggregate device + IOProc). The app needs a reader per tap. |
| Solo (global exclude tap + reader) | **Works.** Pings went silent, QCView kept playing. |
| Transition artefacts | No click; a brief skip in the tapped app when the tap starts or stops. Other apps unaffected. Device buffer/latency/rate unchanged. |
| Taps die with their creator? | **No.** After `kill -9` the tap stayed for over a minute; `destroy --all` from another process removed it. The app must clean up its own leftovers on launch. |
| Permission prompt for a signed CLI | No prompt; an unmuted tap with an IOProc delivered non-silent frames at once. Grant is attributed to the shell's responsible app, so re-check with the signed `.app`. |
| Update a tap in place | `kAudioTapPropertyDescription` set → `noErr`; another process saw the new name on the same UID. |
| Process object IDs | Per client connection (156 vs 157 for the same PID). Key on bundle ID/PID. |
| Private taps | Invisible to other processes' `taps`; use `--public` when checking from a second shell. |

## Listening checklist (needs ears)

Done so far: 1 (fails without `--io`), 2, 4 (with `--io`). Remaining: 3, 5, 6, 7.

Play audio in two apps, e.g. Music and a browser tab. Run each line in one shell and listen.

1. `$B mute <app>` — tap only. Is the app silent? Is the other app unchanged? Any click or latency change in the other app?
2. `$B mute <app> --io` — same, plus an aggregate device and IOProc reading the tap. Does muting only work here? The heartbeat prints frame counts; non-silent buffers > 0 means the tap is receiving the app's audio.
3. `$B mute <app> --when-tapped` then `--when-tapped --io` — expect audible until a reader starts, silent while it runs.
4. `$B solo <app>` — everything except the app should go silent, including an app launched afterwards.
5. While 1 or 2 runs: switch the output device (headphones, Bluetooth, AirPlay). Does the mute follow?
6. `$B mute <app> --watch` then quit and relaunch the app. The spike should print "process list changed" and update the tap; is the relaunched app still muted?
7. Kill the spike with `kill -9` during a muted run, confirm the app stays muted, then `$B destroy --all` to recover.

Record results in `../plans/app-mute-plan.md` under "Open questions".
