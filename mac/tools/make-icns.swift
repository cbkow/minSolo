// Rasterize an SVG with AppKit into an .iconset and build an .icns.
// usage: swift make-icns.swift <in.svg> <out.icns>
import AppKit
let a = CommandLine.arguments
guard a.count == 3, let svg = NSImage(contentsOfFile: a[1]) else { print("usage: make-icns.swift in.svg out.icns"); exit(2) }
let tmp = FileManager.default.temporaryDirectory.appendingPathComponent("minSolo.iconset")
try? FileManager.default.removeItem(at: tmp)
try! FileManager.default.createDirectory(at: tmp, withIntermediateDirectories: true)
for (pt, scale) in [(16,1),(16,2),(32,1),(32,2),(128,1),(128,2),(256,1),(256,2),(512,1),(512,2)] {
    let px = pt * scale
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: px, pixelsHigh: px, bitsPerSample: 8, samplesPerPixel: 4,
                               hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    NSGraphicsContext.current?.imageInterpolation = .high
    svg.draw(in: NSRect(x: 0, y: 0, width: px, height: px), from: .zero, operation: .sourceOver, fraction: 1)
    NSGraphicsContext.restoreGraphicsState()
    let name = scale == 1 ? "icon_\(pt)x\(pt).png" : "icon_\(pt)x\(pt)@2x.png"
    try! rep.representation(using: .png, properties: [:])!.write(to: tmp.appendingPathComponent(name))
}
let p = Process(); p.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
p.arguments = ["-c", "icns", tmp.path, "-o", a[2]]; try! p.run(); p.waitUntilExit()
print(p.terminationStatus == 0 ? "wrote \(a[2])" : "iconutil failed"); exit(p.terminationStatus)
