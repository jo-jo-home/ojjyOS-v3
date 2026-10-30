// Host-only safeguards; no NSApplication or VM is started. Created by Jonas Lee.
import Foundation

func runVMHostTests() throws {
    var count = 0
    func check(_ condition: Bool,_ label: String) throws {
        count += 1
        if !condition { throw HostError(label) }
    }
    for code in 32...126 {
        let char = Character(UnicodeScalar(code)!)
        let key = textKey(char)
        try check(key != nil && usageToMac[key!.0] != nil,"ASCII mapping \(code)")
    }
    try check(textKey("é") == nil,"unsupported Unicode must not inject partial text")
    try check(textKey("A")!.1 && !textKey("a")!.1,"shifted ASCII distinguished")
    try check(LaunchLimits(minimum:512 << 20,maximum:64 << 30,hostMemory:16 << 30).memoryChoices == [2048,3072,4096,6144,8192],"all launch RAM choices remain available after high-RAM update")
    let root = URL(fileURLWithPath:CommandLine.arguments[1]).appendingPathComponent(UUID().uuidString)
    try FileManager.default.createDirectory(at:root,withIntermediateDirectories:true)
    defer { try? FileManager.default.removeItem(at:root) }
    let limits = LaunchLimits(minimum:512 << 20,maximum:64 << 30,hostMemory:16 << 30)
    var messages: [String] = []
    func decode(_ text: String) -> LaunchSettings {
        LaunchSettings.decode(Data(text.utf8),limits:limits) { messages.append($0) }
    }
    let defaults = LaunchSettings()
    try check(decode("{}") == defaults,"partial object defaults")
    try check(decode("{\"unknownFutureKey\":42}") == defaults,"unknown keys ignored")
    for bad in ["", "[1,2]", "null", "{broken", "{\"version\":2}"] {
        messages.removeAll()
        try check(decode(bad) == defaults && !messages.isEmpty,"broken/version JSON defaults and logs")
    }
    messages.removeAll()
    try check(decode("{\"memoryMB\":true,\"network\":1,\"fullScreen\":\"yes\",\"cpus\":1.5}") == defaults,
              "JSON types are not coerced")
    try check(!messages.isEmpty,"invalid types log")
    let clamped = decode("{\"memoryMB\":999999,\"cpus\":8,\"screen\":\"9000x9000\"}")
    try check(clamped.memoryMB == 8192 && clamped.cpus == 1 && clamped.screen == defaults.screen,"clamp unsafe values")
    try check(decode("{\"memoryMB\":-9223372036854775808}").memoryMB == 2048,"Int.min cannot overflow clamp")
    try check(decode("{\"memoryMB\":9223372036854775807}").memoryMB == 8192,"Int.max clamp")
    try check(LaunchLimits(minimum:3 << 30,maximum:6 << 30,hostMemory:8 << 30).memoryChoices == [3072,4096],"VZ+half-host bounds")
    try check(LaunchLimits(minimum:512 << 20,maximum:64 << 30,hostMemory:2 << 30).memoryChoices.isEmpty,"no illegal RAM fallback in UI")
    let settingsFile = root.appendingPathComponent("launch-settings.json")
    messages.removeAll()
    try check(LaunchSettings.load(settingsFile,limits:limits) { messages.append($0) } == defaults && !messages.isEmpty,"missing file logs/defaults")
    var custom = defaults
    custom.memoryMB = 3072; custom.screen = "1600x900"; custom.network = false
    custom.fullScreen = true; custom.showAtStart = false
    try custom.save(settingsFile)
    try check(LaunchSettings.load(settingsFile,limits:limits,log:{ _ in }) == custom,"atomic round trip")
    let old = try FileManager.default.attributesOfItem(atPath:settingsFile.path)[.systemFileNumber] as! NSNumber
    let oldReader = try FileHandle(forReadingFrom:settingsFile)
    try defaults.save(settingsFile)
    let new = try FileManager.default.attributesOfItem(atPath:settingsFile.path)[.systemFileNumber] as! NSNumber
    try check(old != new,"atomic replacement changes inode, never truncates old file")
    try check(LaunchSettings.decode(try oldReader.readToEnd()!,limits:limits,log:{ _ in }) == custom,"open reader retains complete old JSON after atomic rename")
    try oldReader.close()
    try check(try FileManager.default.contentsOfDirectory(atPath:root.path).filter { $0.hasPrefix(".launch-") }.isEmpty,"temporary files cleaned")
    let directoryTarget = root.appendingPathComponent("a-directory")
    try FileManager.default.createDirectory(at:directoryTarget,withIntermediateDirectories:false)
    var saveRejected = false
    do { try custom.save(directoryTarget) } catch { saveRejected = true }
    try check(saveRejected && FileManager.default.fileExists(atPath:directoryTarget.path),"failed rename preserves target")
    let overrides = try Options(arguments:["--start","--memory","3072","--screen","1280x800"],build:root)
    let effective = overrides.applying(to:defaults,limits:limits,log:{ _ in })
    try check(effective.memoryMB == 3072 && effective.screen == "1280x800","CLI overrides settings")
    try check(LaunchSettings.load(settingsFile,limits:limits,log:{ _ in }) == defaults,"CLI does not persist")
    let normal = try Options(arguments:[],build:root)
    try check(normal.showsLaunchMenu(defaults,optionHeld:false),"menu default")
    try check(!normal.showsLaunchMenu(custom,optionHeld:false) && normal.showsLaunchMenu(custom,optionHeld:true),"Option override")
    try check(!overrides.showsLaunchMenu(defaults,optionHeld:true),"--start skips even with Option")
    let controlled = try Options(arguments:["--control","/unused"],build:root)
    try check(!controlled.showsLaunchMenu(defaults,optionHeld:true),"--control cannot hang on menu")
    for args in [["--memory"],["--memory","no"],["--memory","-1"],["--screen","bad"],["--unknown","value"]] {
        var failed = false
        do { _ = try Options(arguments:args,build:root) } catch { failed = true }
        try check(failed,"invalid CLI rejected: \(args)")
    }
    // Deterministic mutations exercise untrusted input, including valid
    // type-changing JSON, without producing thousands of log lines.
    let seed = Array(try custom.encoded())
    for index in 0..<2000 {
        var bytes = seed
        bytes[index % bytes.count] = UInt8(truncatingIfNeeded:index * 31)
        let value = LaunchSettings.decode(Data(bytes),limits:limits,log:{ _ in })
        try check(limits.memoryChoices.contains(value.memoryMB) && value.cpus == 1 && LaunchScreen.parse(value.screen) != nil,"mutated JSON safe")
    }
    let image = root.appendingPathComponent("test.img")
    try Data("private test disk".utf8).write(to:image)
    let link = root.appendingPathComponent("not-a-log.txt")
    try FileManager.default.linkItem(at:image,to:link)
    var rejected = false
    do { _ = try Options(arguments:["--image",image.path,"--vars",root.appendingPathComponent("vars.fd").path,"--serial",link.path]) }
    catch { rejected = true }
    try check(rejected,"hard-linked disk/log aliases rejected before any log truncation")
    try check(try String(contentsOf:image,encoding:.utf8) == "private test disk","disk unchanged on alias rejection")
    for args in [["--image",settingsFile.path],["--vars",settingsFile.path],["--serial",settingsFile.path]] {
        var failed = false
        do { _ = try Options(arguments:args,build:root) } catch { failed = true }
        try check(failed,"launch settings cannot alias a disk/vars/log path")
    }
    let settingsLink = root.appendingPathComponent("settings-hardlink")
    try FileManager.default.linkItem(at:settingsFile,to:settingsLink)
    var settingsAliasRejected = false
    do { _ = try Options(arguments:["--serial",settingsLink.path],build:root) } catch { settingsAliasRejected = true }
    try check(settingsAliasRejected,"hard-linked launch settings cannot be truncated by serial logging")
    func canLock(_ path: URL) throws -> Bool {
        let process = Process()
        process.executableURL = URL(fileURLWithPath:"/usr/bin/python3")
        process.arguments = ["-c","import fcntl,sys\nf=open(sys.argv[1],'r+b')\ntry: fcntl.lockf(f,fcntl.LOCK_EX|fcntl.LOCK_NB)\nexcept BlockingIOError: sys.exit(3)",path.path]
        try process.run(); process.waitUntilExit()
        return process.terminationStatus == 0
    }
    let lease = try DiskLease(image)
    let sidecar = root.appendingPathComponent(".image-update.lock")
    try check(try !canLock(image),"disk byte-range lock excludes another process")
    try check(try !canLock(sidecar),"builder sidecar lock excludes image replacement")
    lease.release()
    try check(try canLock(image) && canLock(sidecar),"locks released after VM lease ends")
    print("PASS: \(count) native-host mapping, alias and disk/builder lock checks")
}
