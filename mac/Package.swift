// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "minSolo",
    platforms: [.macOS("14.2")],
    targets: [
        .executableTarget(
            name: "minSolo",
            path: "Sources/minSolo",
            swiftSettings: [.unsafeFlags(["-parse-as-library"])]
        ),
    ]
)
