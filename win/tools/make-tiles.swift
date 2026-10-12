// Render the MSIX tile set from an SVG with AppKit.
// usage: swift make-tiles.swift <in.svg> <images dir>
import AppKit
let a = CommandLine.arguments
guard a.count == 3, let svg = NSImage(contentsOfFile: a[1]) else { print("usage: make-tiles.swift in.svg outdir"); exit(2) }
let out = URL(fileURLWithPath: a[2])
try? FileManager.default.createDirectory(at: out, withIntermediateDirectories: true)
func render(_ w: Int, _ h: Int, glyph: Int, name: String) {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: w, pixelsHigh: h, bitsPerSample: 8, samplesPerPixel: 4,
                               hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    NSGraphicsContext.current?.imageInterpolation = .high
    let x = (w - glyph) / 2, y = (h - glyph) / 2
    svg.draw(in: NSRect(x: x, y: y, width: glyph, height: glyph), from: .zero, operation: .sourceOver, fraction: 1)
    NSGraphicsContext.restoreGraphicsState()
    try! rep.representation(using: .png, properties: [:])!.write(to: out.appendingPathComponent(name))
}
for sz in [16, 24, 32, 44, 48, 256] {
    render(sz, sz, glyph: sz, name: "Square44x44Logo.targetsize-\(sz).png")
    render(sz, sz, glyph: sz, name: "Square44x44Logo.targetsize-\(sz)_altform-unplated.png")
}
render(44, 44, glyph: 44, name: "Square44x44Logo.png")
render(50, 50, glyph: 50, name: "StoreLogo.png")
render(150, 150, glyph: 150, name: "Square150x150Logo.png")
render(310, 150, glyph: 150, name: "Wide310x150Logo.png")
print("wrote tiles to \(out.path)")
