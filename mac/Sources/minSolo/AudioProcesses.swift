import AppKit
import CoreAudio
import Foundation

/// One Core Audio process object.
struct AudioProc {
    let objectID: AudioObjectID
    let pid: pid_t
    let bundleID: String
    let runningOutput: Bool
}

/// Processes grouped under the app that owns them (Chrome + its helpers, Safari + WebKit.GPU, …).
struct AudioApp: Identifiable, Equatable {
    let id: String            // group key: the owning app's bundle ID, or "pid:<n>" for bare processes
    let name: String
    let pids: [pid_t]
    let objectIDs: [AudioObjectID]
    let isPlaying: Bool       // any process has output running
    let isApp: Bool           // owned by a regular or accessory app (not a daemon/CLI)

    static func == (a: AudioApp, b: AudioApp) -> Bool {
        a.id == b.id && a.objectIDs == b.objectIDs && a.isPlaying == b.isPlaying && a.name == b.name && a.isApp == b.isApp
    }
}

private typealias ResponsibleFn = @convention(c) (pid_t) -> pid_t
private let responsiblePID: ResponsibleFn? = {
    guard let sym = dlsym(UnsafeMutableRawPointer(bitPattern: -2), "responsibility_get_pid_responsible_for_pid") else { return nil }
    return unsafeBitCast(sym, to: ResponsibleFn.self)
}()

func processName(_ pid: pid_t) -> String {
    var buf = [CChar](repeating: 0, count: 4096)
    return proc_name(pid, &buf, UInt32(buf.count)) > 0 ? String(cString: buf) : "pid \(pid)"
}

func audioProcesses() -> [AudioProc] {
    readArray(systemObject, kAudioHardwarePropertyProcessObjectList, as: AudioObjectID.self).map { id in
        AudioProc(objectID: id,
                  pid: readScalar(id, kAudioProcessPropertyPID, pid_t(0)) ?? 0,
                  bundleID: readString(id, kAudioProcessPropertyBundleID) ?? "",
                  runningOutput: (readScalar(id, kAudioProcessPropertyIsRunningOutput, UInt32(0)) ?? 0) != 0)
    }
}

/// Group key + display name for a process: the responsible app if there is one, else the process itself.
private func owner(of p: AudioProc) -> (key: String, name: String, isApp: Bool) {
    var pid = p.pid
    if let f = responsiblePID { let r = f(p.pid); if r > 0 { pid = r } }
    for candidate in [pid, p.pid] {
        if let app = NSRunningApplication(processIdentifier: candidate), let bid = app.bundleIdentifier {
            return (bid, app.localizedName ?? bid, app.activationPolicy != .prohibited)
        }
    }
    if !p.bundleID.isEmpty {
        // "com.google.Chrome.helper" → "com.google.Chrome"
        let key = p.bundleID.hasSuffix(".helper") ? String(p.bundleID.dropLast(7)) : p.bundleID
        return (key, key, false)
    }
    return ("pid:\(p.pid)", processName(p.pid), false)
}

func groupedAudioApps() -> [AudioApp] {
    let me = getpid()
    var groups: [String: (name: String, pids: [pid_t], ids: [AudioObjectID], playing: Bool, isApp: Bool)] = [:]
    var order: [String] = []
    for p in audioProcesses() where p.pid != me {
        let o = owner(of: p)
        if groups[o.key] == nil { groups[o.key] = (o.name, [], [], false, o.isApp); order.append(o.key) }
        groups[o.key]!.pids.append(p.pid)
        groups[o.key]!.ids.append(p.objectID)
        groups[o.key]!.playing = groups[o.key]!.playing || p.runningOutput
    }
    return order.map { k in
        let g = groups[k]!
        return AudioApp(id: k, name: g.name, pids: g.pids, objectIDs: g.ids.sorted(), isPlaying: g.playing, isApp: g.isApp)
    }
}

func appIcon(forKey key: String, pids: [pid_t]) -> NSImage? {
    for pid in pids {
        var p = pid
        if let f = responsiblePID { let r = f(pid); if r > 0 { p = r } }
        if let app = NSRunningApplication(processIdentifier: p), let icon = app.icon { return icon }
    }
    return nil
}

/// Publishes the grouped app list; refreshes on process-list changes and on a 1 s poll
/// (output-running flips do not always come with a list change).
@MainActor
final class ProcessMonitor: ObservableObject {
    @Published private(set) var apps: [AudioApp] = []
    var onChange: (([AudioApp]) -> Void)?
    private var timer: Timer?

    init() {
        refresh()
        listen(systemObject, kAudioHardwarePropertyProcessObjectList) { [weak self] in self?.refresh() }
        timer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.refresh() }
        }
    }

    func refresh() {
        let now = groupedAudioApps()
        guard now != apps else { return }
        apps = now
        onChange?(now)
    }
}
