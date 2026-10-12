import CoreAudio
import Foundation
import OSLog

let logger = Logger(subsystem: "com.bialkowski.minsolo", category: "taps")

let systemObject = AudioObjectID(kAudioObjectSystemObject)

func address(_ selector: AudioObjectPropertySelector,
             scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal) -> AudioObjectPropertyAddress {
    AudioObjectPropertyAddress(mSelector: selector, mScope: scope, mElement: kAudioObjectPropertyElementMain)
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

func readTapDescription(_ tap: AudioObjectID) -> CATapDescription? {
    var addr = address(kAudioTapPropertyDescription)
    var size = UInt32(MemoryLayout<Unmanaged<CATapDescription>?>.size)
    var value: Unmanaged<CATapDescription>? = nil
    guard AudioObjectGetPropertyData(tap, &addr, 0, nil, &size, &value) == noErr else { return nil }
    return value?.takeRetainedValue()
}

func fourcc(_ status: OSStatus) -> String {
    let n = UInt32(bitPattern: status)
    let bytes = [UInt8(n >> 24 & 0xff), UInt8(n >> 16 & 0xff), UInt8(n >> 8 & 0xff), UInt8(n & 0xff)]
    if bytes.allSatisfy({ $0 >= 0x20 && $0 < 0x7f }) { return "'\(String(bytes: bytes, encoding: .ascii)!)'" }
    return "\(status)"
}

/// Listen for a property on an object; the block runs on the main queue.
@discardableResult
func listen(_ object: AudioObjectID, _ selector: AudioObjectPropertySelector, _ block: @escaping () -> Void) -> OSStatus {
    var addr = address(selector)
    return AudioObjectAddPropertyListenerBlock(object, &addr, .main) { _, _ in block() }
}

func log(_ s: String) {
    logger.notice("\(s, privacy: .public)")
}
