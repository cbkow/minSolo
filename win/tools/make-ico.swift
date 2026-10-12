// Rasterize an SVG with AppKit and write a multi-size .ico (PNG-compressed entries).
// usage: swift make-ico.swift <in.svg> <out.ico>
import AppKit
let a = CommandLine.arguments
guard a.count == 3, let svg = NSImage(contentsOfFile: a[1]) else { print("usage: make-ico.swift in.svg out.ico"); exit(2) }
let sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
var pngs: [Data] = []
for px in sizes {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: px, pixelsHigh: px, bitsPerSample: 8, samplesPerPixel: 4,
                               hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    NSGraphicsContext.current?.imageInterpolation = .high
    svg.draw(in: NSRect(x: 0, y: 0, width: px, height: px), from: .zero, operation: .sourceOver, fraction: 1)
    NSGraphicsContext.restoreGraphicsState()
    pngs.append(rep.representation(using: .png, properties: [:])!)
}
var ico = Data()
func le16(_ v: Int) { ico.append(contentsOf: [UInt8(v & 0xff), UInt8(v >> 8 & 0xff)]) }
func le32(_ v: Int) { ico.append(contentsOf: [UInt8(v & 0xff), UInt8(v >> 8 & 0xff), UInt8(v >> 16 & 0xff), UInt8(v >> 24 & 0xff)]) }
le16(0); le16(1); le16(sizes.count)
var offset = 6 + 16 * sizes.count
for (i, px) in sizes.enumerated() {
    ico.append(UInt8(px == 256 ? 0 : px)); ico.append(UInt8(px == 256 ? 0 : px)); ico.append(0); ico.append(0)
    le16(1); le16(32); le32(pngs[i].count); le32(offset)
    offset += pngs[i].count
}
for p in pngs { ico.append(p) }
try! ico.write(to: URL(fileURLWithPath: a[2]))
print("wrote \(a[2]) (\(ico.count) bytes, \(sizes.count) sizes)")
