// Shared launch-file contract and host limits. Created by Jonas Lee.
import Foundation
import CoreFoundation
import Virtualization
import Darwin

struct HostError: Error, CustomStringConvertible {
    let description: String
    init(_ message: String) { description = message }
}

struct LaunchScreen: Equatable {
    let width: Int
    let height: Int
    var id: String { "\(width)x\(height)" }
    var label: String { "\(width) × \(height) Retina" }
    var aspect: Double { Double(width) / Double(height) }
    // Only offer modes with live HW-12 evidence. This is a logical size;
    // the GPU receives twice each dimension, matching fb_ui_scale().
    static let supported = [LaunchScreen(width:1440,height:900),
                            LaunchScreen(width:1280,height:800),
                            LaunchScreen(width:1600,height:900)]
    static func parse(_ value: String) -> LaunchScreen? { supported.first { $0.id == value } }
}

struct LaunchLimits {
    let memoryChoices: [Int]
    init(minimum: UInt64 = VZVirtualMachineConfiguration.minimumAllowedMemorySize,
         maximum: UInt64 = VZVirtualMachineConfiguration.maximumAllowedMemorySize,
         hostMemory: UInt64 = ProcessInfo.processInfo.physicalMemory) {
        memoryChoices = [2048,3072,4096,6144,8192].filter {
            let bytes = UInt64($0) * 1024 * 1024
            return bytes >= minimum && bytes <= maximum && bytes <= hostMemory / 2
        }
    }
    func clamp(_ value: Int) -> Int {
        memoryChoices.min { abs(Double($0) - Double(value)) < abs(Double($1) - Double(value)) } ?? 2048
    }
}

struct LaunchSettings: Equatable {
    var version = 1
    var memoryMB = 2048
    var cpus = 1
    var screen = "1440x900"
    var network = true
    var fullScreen = false
    var showAtStart = true
    var display: LaunchScreen { LaunchScreen.parse(screen) ?? LaunchScreen.supported[0] }

    func validated(_ limits: LaunchLimits, log: (String) -> Void) -> LaunchSettings {
        var result = self
        result.version = 1; result.cpus = 1
        result.memoryMB = limits.clamp(memoryMB)
        if LaunchScreen.parse(screen) == nil { result.screen = "1440x900" }
        if result != self { log("Invalid or unsupported launch values; using clamped/default values") }
        return result
    }
    static func decode(_ data: Data, limits: LaunchLimits, log: (String) -> Void) -> LaunchSettings {
        var result = LaunchSettings()
        do {
            guard data.count <= 65536,
                  let json = try JSONSerialization.jsonObject(with:data) as? [String:Any] else {
                throw HostError("Expected a small JSON object")
            }
            // NSNumber bridges booleans and numbers: reject true as 1 and
            // fractional values as integers rather than silently coercing.
            func integer(_ key: String,_ fallback: Int) -> Int {
                guard let value = json[key] else { return fallback }
                guard let n = value as? NSNumber, CFGetTypeID(n) != CFBooleanGetTypeID(),
                      let parsed = Int(n.stringValue) else { log("Invalid \(key); using default"); return fallback }
                return parsed
            }
            func boolean(_ key: String,_ fallback: Bool) -> Bool {
                guard let value = json[key] else { return fallback }
                guard let n = value as? NSNumber, CFGetTypeID(n) == CFBooleanGetTypeID() else {
                    log("Invalid \(key); using default"); return fallback
                }
                return n.boolValue
            }
            guard integer("version",1) == 1 else { throw HostError("Unsupported launch settings version") }
            result.memoryMB = integer("memoryMB",2048); result.cpus = integer("cpus",1)
            if let value = json["screen"] {
                if let text = value as? String { result.screen = text }
                else { log("Invalid screen; using default") }
            }
            result.network = boolean("network",true)
            result.fullScreen = boolean("fullScreen",false)
            result.showAtStart = boolean("showAtStart",true)
        } catch { log("Cannot read launch settings (\(error)); using defaults") }
        return result.validated(limits,log:log)
    }
    static func load(_ url: URL, limits: LaunchLimits, log: (String) -> Void) -> LaunchSettings {
        do { return decode(try Data(contentsOf:url),limits:limits,log:log) }
        catch { log("Launch settings missing or unreadable; using defaults"); return LaunchSettings().validated(limits,log:log) }
    }
    func encoded() throws -> Data {
        let object: [String:Any] = ["version":1,"memoryMB":memoryMB,"cpus":1,
            "screen":screen,"network":network,"fullScreen":fullScreen,"showAtStart":showAtStart]
        return try JSONSerialization.data(withJSONObject:object,options:[.sortedKeys,.prettyPrinted])
    }
    func save(_ url: URL) throws {
        // Same-directory exclusive temporary file + POSIX atomic rename.
        // Never truncate the old file, even if encoding/writing/rename fails.
        let temporary = url.deletingLastPathComponent().appendingPathComponent(".launch-\(UUID().uuidString).tmp")
        let fd = Darwin.open(temporary.path,O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,mode_t(0o600))
        guard fd >= 0 else { throw HostError("Cannot create launch settings temporary file") }
        defer { Darwin.close(fd); _ = unlink(temporary.path) }
        let data = try encoded()
        try data.withUnsafeBytes { bytes in
            var offset = 0
            while offset < bytes.count {
                let n = Darwin.write(fd,bytes.baseAddress!.advanced(by:offset),bytes.count-offset)
                if n < 0 && errno == EINTR { continue }
                guard n > 0 else { throw HostError("Cannot write launch settings") }
                offset += n
            }
        }
        guard fsync(fd) == 0, rename(temporary.path,url.path) == 0 else { throw HostError("Cannot atomically save launch settings") }
    }
}
