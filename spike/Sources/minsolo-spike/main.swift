// minsolo-spike: answer the open questions in app-mute-plan.md.
//
//   list                         audio processes: object id, pid, bundle id, output running
//   taps                         taps that exist on the system right now (name, mute behavior, processes)
//   destroy <tapID>|--all        destroy a tap (e.g. one orphaned by a crashed creator)
//   mute <bundleID|pid> [opts]   muted tap of that app's processes, held until Ctrl-C
//   solo <bundleID|pid> [opts]   muted global tap excluding that app's processes, held until Ctrl-C
//
// opts:
//   --io            also build a private aggregate device containing the tap and run an
//                   IOProc on it (tests whether muting needs a running tap reader)
//   --when-tapped   use CATapMutedWhenTapped instead of CATapMuted
//   --unmuted       use CATapUnmuted (control: should change nothing audible)
//   --watch         rebuild the tap when the process list changes (mute only)
//   --update-test   after 3 s, rewrite the tap description in place (same processes, new name);
//                   check with `taps` from another shell that the name changed
//   --public        make the tap visible to other processes (so `taps` from another shell can see it)

import CoreAudio
import Foundation

setvbuf(stdout, nil, _IOLBF, 0)  // line-buffered even when piped, so logs survive kill -9

// MARK: - Core Audio property helpers

let systemObject = AudioObjectID(kAudioObjectSystemObject)

func address(_ selector: AudioObjectPropertySelector) -> AudioObjectPropertyAddress {
    AudioObjectPropertyAddress(mSelector: selector,
                               mScope: kAudioObjectPropertyScopeGlobal,
                               mElement: kAudioObjectPropertyElementMain)
}

func readArray<T: Numeric>(_ object: AudioObjectID, _ selector: AudioObjectPropertySelector, as: T.Type) -> [T] {
    var addr = address(selector)
    var size: UInt32 = 0
    guard AudioObjectGetPropertyDataSize(object, &addr, 0, nil, &size) == noErr, size > 0 else { return [] }
    var out = [T](repeating: T.zero, count: Int(size) / MemoryLayout<T>.stride)
    guard AudioObjectGetPropertyData(object, &addr, 0, nil, &size, &out) == noErr else { return [] }
    return out
}

func readScalar<T>(_ object: AudioObjectID, _ selector: AudioObjectPropertySelector, _ zero: T) -> T? {
    var addr = address(selector)
    var size = UInt32(MemoryLayout<T>.size)
    var value = zero
    guard AudioObjectGetPropertyData(object, &addr, 0, nil, &size, &value) == noErr else { return nil }
    return value
}

func readString(_ object: AudioObjectID, _ selector: AudioObjectPropertySelector) -> String? {
    var addr = address(selector)
    var size = UInt32(MemoryLayout<CFString?>.size)
    var value: Unmanaged<CFString>? = nil
    guard AudioObjectGetPropertyData(object, &addr, 0, nil, &size, &value) == noErr,
          let cf = value?.takeRetainedValue() else { return nil }
    return cf as String
}

func fourcc(_ status: OSStatus) -> String {
    let n = UInt32(bitPattern: status)
    let bytes = [UInt8(n >> 24 & 0xff), UInt8(n >> 16 & 0xff), UInt8(n >> 8 & 0xff), UInt8(n & 0xff)]
    if bytes.allSatisfy({ $0 >= 0x20 && $0 < 0x7f }) { return "'\(String(bytes: bytes, encoding: .ascii)!)'" }
    return "\(status)"
}

// MARK: - Processes

struct AudioProcess {
    let objectID: AudioObjectID
    let pid: pid_t
    let bundleID: String
    let runningOutput: Bool
    let running: Bool
}

func audioProcesses() -> [AudioProcess] {
    readArray(systemObject, kAudioHardwarePropertyProcessObjectList, as: AudioObjectID.self).map { id in
        AudioProcess(objectID: id,
                     pid: readScalar(id, kAudioProcessPropertyPID, pid_t(0)) ?? 0,
                     bundleID: readString(id, kAudioProcessPropertyBundleID) ?? "",
                     runningOutput: (readScalar(id, kAudioProcessPropertyIsRunningOutput, UInt32(0)) ?? 0) != 0,
                     running: (readScalar(id, kAudioProcessPropertyIsRunning, UInt32(0)) ?? 0) != 0)
    }
}

func processName(_ pid: pid_t) -> String {
    var buf = [CChar](repeating: 0, count: 4096)
    let n = proc_name(pid, &buf, UInt32(buf.count))
    return n > 0 ? String(cString: buf) : "?"
}

/// Processes belonging to `target`: an exact PID, an exact bundle ID, or a bundle-ID prefix
/// (so "com.google.Chrome" also matches "com.google.Chrome.helper").
func matchingProcesses(_ target: String) -> [AudioProcess] {
    let all = audioProcesses()
    if let pid = pid_t(target) { return all.filter { $0.pid == pid } }
    return all.filter { $0.bundleID == target || $0.bundleID.hasPrefix(target + ".") }
}

func printProcesses() {
    let procs = audioProcesses().sorted { ($0.runningOutput ? 0 : 1, $0.bundleID) < ($1.runningOutput ? 0 : 1, $1.bundleID) }
    func pad(_ s: String, _ w: Int) -> String { s.count >= w ? s + " " : s + String(repeating: " ", count: w - s.count) }
    print(pad("objID", 6) + pad("pid", 7) + pad("out", 5) + pad("running", 8) + pad("bundleID", 44) + "name")
    for p in procs {
        print(pad(String(p.objectID), 6) + pad(String(p.pid), 7) + pad(p.runningOutput ? "YES" : "-", 5)
              + pad(p.running ? "yes" : "-", 8) + pad(p.bundleID.isEmpty ? "(none)" : p.bundleID, 44) + processName(p.pid))
    }
}

func tapDescription(_ tap: AudioObjectID) -> CATapDescription? {
    var addr = address(kAudioTapPropertyDescription)
    var size = UInt32(MemoryLayout<Unmanaged<CATapDescription>?>.size)
    var value: Unmanaged<CATapDescription>? = nil
    guard AudioObjectGetPropertyData(tap, &addr, 0, nil, &size, &value) == noErr else { return nil }
    return value?.takeRetainedValue()
}

func deviceName(_ dev: AudioObjectID) -> String {
    let name = readString(dev, kAudioObjectPropertyName) ?? "?"
    let uid = readString(dev, kAudioDevicePropertyDeviceUID) ?? "?"
    return "\(dev) \"\(name)\" uid=\(uid)"
}

func printInfo(_ target: String) {
    let def = readScalar(systemObject, kAudioHardwarePropertyDefaultOutputDevice, AudioObjectID(0)) ?? 0
    let sys = readScalar(systemObject, kAudioHardwarePropertyDefaultSystemOutputDevice, AudioObjectID(0)) ?? 0
    var bufAddr = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyBufferFrameSize, mScope: kAudioObjectPropertyScopeOutput, mElement: kAudioObjectPropertyElementMain)
    var buf: UInt32 = 0; var bsz = UInt32(4); AudioObjectGetPropertyData(def, &bufAddr, 0, nil, &bsz, &buf)
    var latAddr = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyLatency, mScope: kAudioObjectPropertyScopeOutput, mElement: kAudioObjectPropertyElementMain)
    var lat: UInt32 = 0; var lsz = UInt32(4); AudioObjectGetPropertyData(def, &latAddr, 0, nil, &lsz, &lat)
    var srAddr = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyNominalSampleRate, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
    var sr: Double = 0; var ssz = UInt32(8); AudioObjectGetPropertyData(def, &srAddr, 0, nil, &ssz, &sr)
    print("default output device: \(deviceName(def))  bufferFrames=\(buf) deviceLatency=\(lat) rate=\(sr)")
    print("system output device:  \(deviceName(sys))")
    for p in matchingProcesses(target) {
        let devs = readArray(p.objectID, kAudioProcessPropertyDevices, as: AudioObjectID.self)
        print("process \(p.objectID) pid \(p.pid) \(p.bundleID) output=\(p.runningOutput) devices:")
        for d in devs {
            var hog = address(kAudioDevicePropertyHogMode); hog.mScope = kAudioObjectPropertyScopeOutput
            let hogPid = readScalar(d, kAudioDevicePropertyHogMode, pid_t(-1)) ?? -1
            print("   \(deviceName(d))  hogMode pid=\(hogPid)")
        }
    }
}

func printTaps() {
    let taps = readArray(systemObject, kAudioHardwarePropertyTapList, as: AudioObjectID.self)
    if taps.isEmpty { print("no taps on the system"); return }
    for t in taps {
        let uid = readString(t, kAudioTapPropertyUID) ?? "?"
        if let d = tapDescription(t) {
            print("tap \(t)  uid \(uid)  name \"\(d.name)\"  mute \(d.muteBehavior.rawValue)  exclusive \(d.isExclusive)  processes \(d.processes)")
        } else {
            print("tap \(t)  uid \(uid)  (description unreadable)")
        }
    }
}

func destroyTaps(_ which: String) {
    let taps = readArray(systemObject, kAudioHardwarePropertyTapList, as: AudioObjectID.self)
    let targets = which == "--all" ? taps : taps.filter { String($0) == which }
    if targets.isEmpty { print("no matching tap"); return }
    for t in targets {
        let st = AudioHardwareDestroyProcessTap(t)
        print("destroy tap \(t): \(st == noErr ? "ok" : fourcc(st))")
    }
}

// MARK: - Tap + optional aggregate device

enum Mode { case mute, solo }

final class Session {
    var tapID = AudioObjectID(kAudioObjectUnknown)
    var aggregateID = AudioObjectID(kAudioObjectUnknown)
    var ioProcID: AudioDeviceIOProcID?
    let tapUUID = UUID()
    var isPublic = false
    var name = "minsolo-spike"
    var frames: UInt64 = 0
    var nonSilentBuffers: UInt64 = 0

    func createTap(mode: Mode, ids: [AudioObjectID], behavior: CATapMuteBehavior) -> OSStatus {
        let desc: CATapDescription
        switch mode {
        case .mute: desc = CATapDescription(stereoMixdownOfProcesses: ids)
        case .solo: desc = CATapDescription(stereoGlobalTapButExcludeProcesses: ids)
        }
        desc.uuid = tapUUID
        desc.name = name
        desc.isPrivate = !isPublic
        desc.muteBehavior = behavior
        var id = AudioObjectID(kAudioObjectUnknown)
        let st = AudioHardwareCreateProcessTap(desc, &id)
        if st == noErr { tapID = id }
        return st
    }

    /// Replace the tap's process list in place (kAudioTapPropertyDescription is settable).
    func updateTap(mode: Mode, ids: [AudioObjectID], behavior: CATapMuteBehavior) -> OSStatus {
        let desc: CATapDescription
        switch mode {
        case .mute: desc = CATapDescription(stereoMixdownOfProcesses: ids)
        case .solo: desc = CATapDescription(stereoGlobalTapButExcludeProcesses: ids)
        }
        desc.uuid = tapUUID
        desc.name = name
        desc.isPrivate = !isPublic
        desc.muteBehavior = behavior
        var addr = address(kAudioTapPropertyDescription)
        var unmanaged: Unmanaged<CATapDescription>? = Unmanaged.passUnretained(desc)
        return AudioObjectSetPropertyData(tapID, &addr, 0, nil, UInt32(MemoryLayout<Unmanaged<CATapDescription>?>.size), &unmanaged)
    }

    func startIO() -> OSStatus {
        guard let outputID = readScalar(systemObject, kAudioHardwarePropertyDefaultOutputDevice, AudioObjectID(0)),
              let outputUID = readString(outputID, kAudioDevicePropertyDeviceUID) else { return -1 }
        let description: [String: Any] = [
            kAudioAggregateDeviceNameKey: "minsolo-spike aggregate",
            kAudioAggregateDeviceUIDKey: UUID().uuidString,
            kAudioAggregateDeviceMainSubDeviceKey: outputUID,
            kAudioAggregateDeviceIsPrivateKey: true,
            kAudioAggregateDeviceIsStackedKey: false,
            kAudioAggregateDeviceTapAutoStartKey: true,
            kAudioAggregateDeviceSubDeviceListKey: [[kAudioSubDeviceUIDKey: outputUID]],
            kAudioAggregateDeviceTapListKey: [[kAudioSubTapDriftCompensationKey: true,
                                               kAudioSubTapUIDKey: tapUUID.uuidString]],
        ]
        var agg = AudioObjectID(kAudioObjectUnknown)
        var st = AudioHardwareCreateAggregateDevice(description as CFDictionary, &agg)
        guard st == noErr else { return st }
        aggregateID = agg
        let queue = DispatchQueue(label: "minsolo.spike.io")
        st = AudioDeviceCreateIOProcIDWithBlock(&ioProcID, agg, queue) { [weak self] _, inData, _, _, _ in
            guard let self else { return }
            let buffers = UnsafeMutableAudioBufferListPointer(UnsafeMutablePointer(mutating: inData))
            for b in buffers {
                guard let p = b.mData else { continue }
                let n = Int(b.mDataByteSize) / MemoryLayout<Float>.size
                self.frames += UInt64(n)
                let f = p.bindMemory(to: Float.self, capacity: n)
                if (0..<n).contains(where: { abs(f[$0]) > 1e-6 }) { self.nonSilentBuffers += 1 }
            }
        }
        guard st == noErr else { return st }
        return AudioDeviceStart(agg, ioProcID)
    }

    func tearDown() {
        if let p = ioProcID, aggregateID != kAudioObjectUnknown {
            AudioDeviceStop(aggregateID, p)
            AudioDeviceDestroyIOProcID(aggregateID, p)
        }
        if aggregateID != kAudioObjectUnknown { AudioHardwareDestroyAggregateDevice(aggregateID) }
        if tapID != kAudioObjectUnknown {
            let st = AudioHardwareDestroyProcessTap(tapID)
            print("destroy tap \(tapID): \(st == noErr ? "ok" : fourcc(st))")
        }
    }
}

// MARK: - main

let args = Array(CommandLine.arguments.dropFirst())
guard let command = args.first else {
    print("usage: minsolo-spike list | taps | mute <bundleID|pid> [--io] [--when-tapped|--unmuted] [--watch] | solo <bundleID|pid> [--io]")
    exit(2)
}

switch command {
case "list": printProcesses(); exit(0)
case "taps": printTaps(); exit(0)
case "info": printInfo(args.count >= 2 ? args[1] : ""); exit(0)
case "destroy":
    guard args.count >= 2 else { print("destroy <tapID>|--all"); exit(2) }
    destroyTaps(args[1]); exit(0)
case "mute", "solo": break
default: print("unknown command \(command)"); exit(2)
}

guard args.count >= 2 else { print("need a bundle ID or pid"); exit(2) }
let mode: Mode = command == "mute" ? .mute : .solo
let target = args[1]
let flags = Set(args.dropFirst(2))
let behavior: CATapMuteBehavior = flags.contains("--when-tapped") ? .mutedWhenTapped : flags.contains("--unmuted") ? .unmuted : .muted

var matches = matchingProcesses(target)
if matches.isEmpty && mode == .mute { print("no audio process matches \(target); run `list`"); exit(1) }
print("\(mode) \(target): \(matches.count) process object(s): " + matches.map { "\($0.objectID)/\($0.pid)/\($0.bundleID)" }.joined(separator: ", "))
print("behavior: \(behavior)   io: \(flags.contains("--io"))")

let session = Session()
session.isPublic = flags.contains("--public")
let st = session.createTap(mode: mode, ids: matches.map(\.objectID), behavior: behavior)
guard st == noErr else { print("AudioHardwareCreateProcessTap failed: \(fourcc(st))"); exit(1) }
print("tap \(session.tapID) created (uid \(readString(session.tapID, kAudioTapPropertyUID) ?? "?"))")

if flags.contains("--io") {
    let st = session.startIO()
    print(st == noErr ? "aggregate \(session.aggregateID) running with IOProc" : "startIO failed: \(fourcc(st))")
}

// Ctrl-C: tear down cleanly. (Test the "taps die with their creator" question with kill -9 instead.)
signal(SIGINT, SIG_IGN)
let sigint = DispatchSource.makeSignalSource(signal: SIGINT, queue: .main)
sigint.setEventHandler {
    print("\nCtrl-C")
    session.tearDown()
    exit(0)
}
sigint.resume()

// --watch: listen for process list changes and update the tap in place.
if flags.contains("--watch") && mode == .mute {
    var addr = address(kAudioHardwarePropertyProcessObjectList)
    AudioObjectAddPropertyListenerBlock(systemObject, &addr, .main) { _, _ in
        let now = matchingProcesses(target)
        guard now.map(\.objectID) != matches.map(\.objectID) else { return }
        matches = now
        let st = session.updateTap(mode: mode, ids: now.map(\.objectID), behavior: behavior)
        print("process list changed → \(now.count) match(es); update tap in place: \(st == noErr ? "ok" : fourcc(st))")
    }
    print("watching process list")
}

if flags.contains("--update-test") {
    DispatchQueue.main.asyncAfter(deadline: .now() + 3) {
        session.name = "minsolo-spike-updated"
        let st = session.updateTap(mode: mode, ids: matches.map(\.objectID), behavior: behavior)
        print("update-test: set kAudioTapPropertyDescription → \(st == noErr ? "ok" : fourcc(st))")
    }
}

// Heartbeat so --io shows whether the reader is receiving frames.
var tick = 0
Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { _ in
    tick += 5
    var line = "[\(tick)s] holding tap \(session.tapID)"
    if flags.contains("--io") { line += "  frames \(session.frames)  non-silent buffers \(session.nonSilentBuffers)" }
    print(line)
}

print("holding. Listen: is \(target) silent? Are other apps unaffected? Ctrl-C to release.")
RunLoop.main.run()
