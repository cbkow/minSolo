import CoreAudio
import Foundation

let tapNamePrefix = "minSolo "
let aggregateUIDPrefix = "minSolo-"

/// A muted process tap plus the private aggregate device + idle IOProc that makes the mute take effect
/// (verified in the spike: a muted tap with no reader captures but does not silence).
final class TapHandle {
    enum Kind { case mute(ids: [AudioObjectID]), solo(exclude: [AudioObjectID]) }

    let name: String
    let uuid = UUID()
    private(set) var tapID = AudioObjectID(kAudioObjectUnknown)
    private(set) var aggregateID = AudioObjectID(kAudioObjectUnknown)
    private var ioProcID: AudioDeviceIOProcID?
    private var kind: Kind

    init?(name: String, kind: Kind) {
        self.name = name
        self.kind = kind
        var id = AudioObjectID(kAudioObjectUnknown)
        let st = AudioHardwareCreateProcessTap(description(), &id)
        guard st == noErr else { log("create tap \(name) failed: \(fourcc(st))"); return nil }
        tapID = id
        let io = startReader()
        if io != noErr { log("reader for \(name) failed: \(fourcc(io)) (tap exists but will not silence)") }
        log("tap \(tapID) \(name) up, aggregate \(aggregateID)")
    }

    private func description() -> CATapDescription {
        let d: CATapDescription
        switch kind {
        case .mute(let ids): d = CATapDescription(stereoMixdownOfProcesses: ids)
        case .solo(let ex): d = CATapDescription(stereoGlobalTapButExcludeProcesses: ex)
        }
        d.uuid = uuid
        d.name = name
        d.isPrivate = false          // visible, so a later launch can sweep leftovers after a crash
        d.muteBehavior = .muted
        return d
    }

    /// Rewrite the process list in place (same UID, no destroy/recreate).
    func update(_ newKind: Kind) -> OSStatus {
        kind = newKind
        var addr = address(kAudioTapPropertyDescription)
        var unmanaged: Unmanaged<CATapDescription>? = Unmanaged.passUnretained(description())
        let st = AudioObjectSetPropertyData(tapID, &addr, 0, nil, UInt32(MemoryLayout<Unmanaged<CATapDescription>?>.size), &unmanaged)
        if st != noErr { log("update tap \(name) failed: \(fourcc(st))") }
        return st
    }

    func matches(_ other: Kind) -> Bool {
        switch (kind, other) {
        case (.mute(let a), .mute(let b)): return a == b
        case (.solo(let a), .solo(let b)): return a == b
        default: return false
        }
    }

    private func startReader() -> OSStatus {
        guard let outputID = readScalar(systemObject, kAudioHardwarePropertyDefaultOutputDevice, AudioObjectID(0)),
              let outputUID = readString(outputID, kAudioDevicePropertyDeviceUID) else { return -1 }
        let desc: [String: Any] = [
            kAudioAggregateDeviceNameKey: name,
            kAudioAggregateDeviceUIDKey: aggregateUIDPrefix + uuid.uuidString,
            kAudioAggregateDeviceMainSubDeviceKey: outputUID,
            kAudioAggregateDeviceIsPrivateKey: true,
            kAudioAggregateDeviceIsStackedKey: false,
            kAudioAggregateDeviceTapAutoStartKey: true,
            kAudioAggregateDeviceSubDeviceListKey: [[kAudioSubDeviceUIDKey: outputUID]],
            kAudioAggregateDeviceTapListKey: [[kAudioSubTapDriftCompensationKey: true,
                                               kAudioSubTapUIDKey: uuid.uuidString]],
        ]
        var agg = AudioObjectID(kAudioObjectUnknown)
        var st = AudioHardwareCreateAggregateDevice(desc as CFDictionary, &agg)
        guard st == noErr else { return st }
        aggregateID = agg
        st = AudioDeviceCreateIOProcIDWithBlock(&ioProcID, agg, DispatchQueue(label: "minSolo.reader")) { _, _, _, _, _ in
            // Discard. The reader only has to exist for the mute to take effect.
        }
        guard st == noErr else { return st }
        return AudioDeviceStart(agg, ioProcID)
    }

    private func stopReader() {
        if let p = ioProcID, aggregateID != kAudioObjectUnknown {
            AudioDeviceStop(aggregateID, p)
            AudioDeviceDestroyIOProcID(aggregateID, p)
            ioProcID = nil
        }
        if aggregateID != kAudioObjectUnknown {
            AudioHardwareDestroyAggregateDevice(aggregateID)
            aggregateID = kAudioObjectUnknown
        }
    }

    /// The aggregate follows the default output device; rebuild it when that changes.
    func rebuildReader() {
        stopReader()
        let st = startReader()
        log("rebuilt reader for \(name): \(st == noErr ? "ok" : fourcc(st))")
    }

    func destroy() {
        stopReader()
        if tapID != kAudioObjectUnknown {
            let st = AudioHardwareDestroyProcessTap(tapID)
            log("destroy tap \(tapID) \(name): \(st == noErr ? "ok" : fourcc(st))")
            tapID = AudioObjectID(kAudioObjectUnknown)
        }
    }
}

/// Owns the mute/solo state and reconciles it against the live process list.
@MainActor
final class TapEngine: ObservableObject {
    @Published private(set) var muted: Set<String> = []
    @Published private(set) var soloed: Set<String> = []
    /// Last seen name per app key, so muted apps that quit still have a label.
    @Published private(set) var names: [String: String] = [:]

    private var muteTaps: [String: TapHandle] = [:]
    private var soloTap: TapHandle?
    private var apps: [String: AudioApp] = [:]

    init() {
        sweepLeftovers()
        listen(systemObject, kAudioHardwarePropertyDefaultOutputDevice) { [weak self] in
            guard let self else { return }
            log("default output device changed")
            for h in self.muteTaps.values { h.rebuildReader() }
            self.soloTap?.rebuildReader()
        }
    }

    func isMuted(_ key: String) -> Bool { muted.contains(key) }
    func isSoloed(_ key: String) -> Bool { soloed.contains(key) }

    func setMuted(_ key: String, _ on: Bool) {
        if on { muted.insert(key); soloed.remove(key) } else { muted.remove(key) }
        reconcile()
    }

    func setSoloed(_ key: String, _ on: Bool) {
        if on { soloed.insert(key); muted.remove(key) } else { soloed.remove(key) }
        reconcile()
    }

    func clearAll() {
        muted.removeAll(); soloed.removeAll()
        reconcile()
    }

    func update(apps list: [AudioApp]) {
        apps = Dictionary(uniqueKeysWithValues: list.map { ($0.id, $0) })
        for a in list { names[a.id] = a.name }
        reconcile()
    }

    private func reconcile() {
        // Mute taps: one per muted app that currently has processes.
        for key in muteTaps.keys where !muted.contains(key) {
            muteTaps.removeValue(forKey: key)?.destroy()
        }
        for key in muted {
            let ids = apps[key]?.objectIDs ?? []
            if ids.isEmpty {
                muteTaps.removeValue(forKey: key)?.destroy()
            } else if let h = muteTaps[key] {
                if !h.matches(.mute(ids: ids)) { _ = h.update(.mute(ids: ids)) }
            } else {
                muteTaps[key] = TapHandle(name: tapNamePrefix + "mute " + key, kind: .mute(ids: ids))
            }
        }
        // Solo: one global tap excluding every soloed app's processes.
        let exclude = soloed.flatMap { apps[$0]?.objectIDs ?? [] }.sorted()
        if soloed.isEmpty || exclude.isEmpty {
            soloTap?.destroy(); soloTap = nil
        } else if let s = soloTap {
            if !s.matches(.solo(exclude: exclude)) { _ = s.update(.solo(exclude: exclude)) }
        } else {
            soloTap = TapHandle(name: tapNamePrefix + "solo", kind: .solo(exclude: exclude))
        }
    }

    /// Taps outlive a crashed creator (spike finding), so remove any of ours on launch.
    func sweepLeftovers() {
        for t in readArray(systemObject, kAudioHardwarePropertyTapList, as: AudioObjectID.self) {
            if let d = readTapDescription(t), d.name.hasPrefix(tapNamePrefix) {
                let st = AudioHardwareDestroyProcessTap(t)
                log("swept leftover tap \(t) \"\(d.name)\": \(st == noErr ? "ok" : fourcc(st))")
            }
        }
        for dev in readArray(systemObject, kAudioHardwarePropertyDevices, as: AudioObjectID.self) {
            if let uid = readString(dev, kAudioDevicePropertyDeviceUID), uid.hasPrefix(aggregateUIDPrefix) {
                let st = AudioHardwareDestroyAggregateDevice(dev)
                log("swept leftover aggregate \(dev) \(uid): \(st == noErr ? "ok" : fourcc(st))")
            }
        }
    }

    func tearDownAll() {
        for h in muteTaps.values { h.destroy() }
        muteTaps.removeAll()
        soloTap?.destroy(); soloTap = nil
    }
}
