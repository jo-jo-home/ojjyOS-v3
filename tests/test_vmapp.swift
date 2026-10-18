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
    let root = URL(fileURLWithPath:CommandLine.arguments[1]).appendingPathComponent(UUID().uuidString)
    try FileManager.default.createDirectory(at:root,withIntermediateDirectories:true)
    defer { try? FileManager.default.removeItem(at:root) }
    let image = root.appendingPathComponent("test.img")
    try Data("private test disk".utf8).write(to:image)
    let link = root.appendingPathComponent("not-a-log.txt")
    try FileManager.default.linkItem(at:image,to:link)
    var rejected = false
    do { _ = try Options(arguments:["--image",image.path,"--vars",root.appendingPathComponent("vars.fd").path,"--serial",link.path]) }
    catch { rejected = true }
    try check(rejected,"hard-linked disk/log aliases rejected before any log truncation")
    try check(try String(contentsOf:image,encoding:.utf8) == "private test disk","disk unchanged on alias rejection")
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
