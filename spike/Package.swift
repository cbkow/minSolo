// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "minsolo-spike",
    platforms: [.macOS("14.2")],
    targets: [
        .executableTarget(
            name: "minsolo-spike",
            path: "Sources/minsolo-spike",
            linkerSettings: [
                // Embed Info.plist (NSAudioCaptureUsageDescription) in the bare CLI binary.
                .unsafeFlags(["-Xlinker", "-sectcreate",
                              "-Xlinker", "__TEXT",
                              "-Xlinker", "__info_plist",
                              "-Xlinker", "Sources/minsolo-spike/Info.plist"]),
            ]
        ),
    ]
)
