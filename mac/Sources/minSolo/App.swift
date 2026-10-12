import AppKit
import SwiftUI

@MainActor
final class Model: ObservableObject {
    static let shared = Model()
    let monitor = ProcessMonitor()
    let engine = TapEngine()

    private init() {
        monitor.onChange = { [engine] apps in engine.update(apps: apps) }
        engine.update(apps: monitor.apps)
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        _ = Model.shared
        // SIGTERM (logout, pkill): release every tap before dying, or the apps stay muted.
        signal(SIGTERM, SIG_IGN)
        let src = DispatchSource.makeSignalSource(signal: SIGTERM, queue: .main)
        src.setEventHandler { Model.shared.engine.tearDownAll(); exit(0) }
        src.resume()
        sigtermSource = src
    }
    func applicationWillTerminate(_ notification: Notification) {
        Model.shared.engine.tearDownAll()
    }
    private var sigtermSource: DispatchSourceSignal?
}

@main
struct MinSoloApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var delegate

    var body: some Scene {
        MenuBarExtra {
            ContentView(model: Model.shared)
        } label: {
            Image(nsImage: menuBarIcon())
        }
        .menuBarExtraStyle(.window)
    }
}

/// The mark from assets/icons as a template image: alpha only, so it follows the menu bar's colour.
func menuBarIcon() -> NSImage {
    let exe = Bundle.main.executableURL ?? URL(fileURLWithPath: CommandLine.arguments[0])
    let candidates = [
        Bundle.main.resourceURL?.appendingPathComponent("minsoloMAC.svg"),
        exe.deletingLastPathComponent().appendingPathComponent("../../Resources/minsoloMAC.svg"),  // swift build layout
        exe.deletingLastPathComponent().appendingPathComponent("../../../Resources/minsoloMAC.svg"),
    ].compactMap { $0 }
    for url in candidates {
        if let img = NSImage(contentsOf: url.standardizedFileURL) {
            img.size = NSSize(width: 18, height: 18)
            img.isTemplate = true
            return img
        }
    }
    let fallback = NSImage(systemSymbolName: "speaker.slash.fill", accessibilityDescription: "minSolo")!
    fallback.isTemplate = true
    return fallback
}

struct ContentView: View {
    @ObservedObject var model: Model
    @ObservedObject var monitor: ProcessMonitor
    @ObservedObject var engine: TapEngine

    init(model: Model) {
        self.model = model
        self.monitor = model.monitor
        self.engine = model.engine
    }

    /// Apps playing audio now, plus any that are muted/soloed even if silent or quit.
    private var rows: [AudioApp] {
        let live = monitor.apps
        var out = live.filter { $0.isPlaying || engine.isMuted($0.id) || engine.isSoloed($0.id) }
        let shown = Set(out.map(\.id))
        for key in engine.muted.union(engine.soloed) where !shown.contains(key) {
            out.append(AudioApp(id: key, name: engine.names[key] ?? key, pids: [], objectIDs: [], isPlaying: false))
        }
        return out.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            if rows.isEmpty {
                Text("No apps are playing audio")
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, minHeight: 44)
            } else {
                ForEach(rows) { app in
                    AppRow(app: app, engine: engine)
                }
            }
            Divider().padding(.vertical, 6)
            HStack {
                Button("Clear all") { engine.clearAll() }
                    .disabled(engine.muted.isEmpty && engine.soloed.isEmpty)
                Spacer()
                Button("Quit") { NSApplication.shared.terminate(nil) }
                    .keyboardShortcut("q")
            }
            .buttonStyle(.plain)
            .foregroundStyle(.secondary)
            .font(.callout)
        }
        .padding(12)
        .frame(width: 300)
    }
}

struct AppRow: View {
    let app: AudioApp
    @ObservedObject var engine: TapEngine

    var body: some View {
        HStack(spacing: 8) {
            Group {
                if let icon = appIcon(forKey: app.id, pids: app.pids) {
                    Image(nsImage: icon).resizable()
                } else {
                    Image(systemName: "app.dashed").resizable().foregroundStyle(.secondary)
                }
            }
            .frame(width: 20, height: 20)
            VStack(alignment: .leading, spacing: 0) {
                Text(app.name).lineLimit(1)
                if app.objectIDs.isEmpty {
                    Text("not running").font(.caption2).foregroundStyle(.tertiary)
                } else if !app.isPlaying {
                    Text("silent").font(.caption2).foregroundStyle(.tertiary)
                }
            }
            Spacer()
            ToggleChip(label: "M", on: engine.isMuted(app.id), tint: .red) {
                engine.setMuted(app.id, !engine.isMuted(app.id))
            }
            ToggleChip(label: "S", on: engine.isSoloed(app.id), tint: .yellow) {
                engine.setSoloed(app.id, !engine.isSoloed(app.id))
            }
        }
        .padding(.vertical, 4)
    }
}

struct ToggleChip: View {
    let label: String
    let on: Bool
    let tint: Color
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(label)
                .font(.system(size: 12, weight: .bold, design: .rounded))
                .frame(width: 26, height: 22)
                .background(on ? tint : Color.secondary.opacity(0.15), in: RoundedRectangle(cornerRadius: 6))
                .foregroundStyle(on ? Color.black.opacity(0.85) : Color.primary)
        }
        .buttonStyle(.plain)
        .help(label == "M" ? "Mute" : "Solo")
    }
}
