// Native Apple Silicon VM host. Created by Jonas Lee.
import AppKit
import Virtualization
import Darwin

struct Options {
    var image: URL
    var variables: URL
    var serial: URL
    var control: String?
    var startImmediately = false
    var memoryMB: Int?
    var screen: String?
    var launchCapture: String?
    var appearance: String?
    var settingsURL: URL
    init(arguments: [String] = Array(CommandLine.arguments.dropFirst()),
         build: URL = Bundle.main.bundleURL.deletingLastPathComponent()) throws {
        settingsURL = build.appendingPathComponent("launch-settings.json")
        image = build.appendingPathComponent("aarch64/ojjyos3.img")
        variables = build.appendingPathComponent("aarch64/efi-vars.fd")
        serial = build.appendingPathComponent("aarch64/serial-vz.log")
        var args = arguments
        while !args.isEmpty {
            let flag = args.removeFirst()
            if flag == "--start" { startImmediately = true; continue }
            guard !args.isEmpty else { throw HostError("Missing value for \(flag)") }
            let value = args.removeFirst()
            switch flag {
            case "--image": image = URL(fileURLWithPath: value)
            case "--vars": variables = URL(fileURLWithPath: value)
            case "--serial": serial = URL(fileURLWithPath: value)
            case "--control": control = value
            case "--memory":
                guard let mb = Int(value), mb > 0 else { throw HostError("Invalid memory MB: \(value)") }
                memoryMB = mb
            case "--screen":
                guard LaunchScreen.parse(value) != nil else { throw HostError("Unsupported screen: \(value)") }
                screen = value
            // Test-only own-window capture. It never starts a VM or saves settings.
            case "--capture-launch": launchCapture = value
            case "--appearance":
                guard ["light","dark"].contains(value) else { throw HostError("Appearance must be light or dark") }
                appearance = value
            default: throw HostError("Unknown option: \(flag)")
            }
        }
        let paths = [image, variables, serial].map { $0.resolvingSymlinksInPath().standardizedFileURL.path }
        guard Set(paths).count == paths.count else { throw HostError("Disk, EFI variables and serial log must be different files") }
        var identities: Set<String> = []
        for path in paths where FileManager.default.fileExists(atPath:path) {
            let attrs = try FileManager.default.attributesOfItem(atPath:path)
            let identity = "\(attrs[.systemNumber]!)!\(attrs[.systemFileNumber]!)"
            guard identities.insert(identity).inserted else { throw HostError("Disk, EFI variables and serial log must not be hard links to the same file") }
        }
        let settingsPath = settingsURL.resolvingSymlinksInPath().standardizedFileURL.path
        guard !paths.contains(settingsPath) else { throw HostError("Launch settings must be separate from disk, EFI variables and serial log") }
        if FileManager.default.fileExists(atPath:settingsPath) {
            let attrs = try FileManager.default.attributesOfItem(atPath:settingsPath)
            let identity = "\(attrs[.systemNumber]!)!\(attrs[.systemFileNumber]!)"
            guard !identities.contains(identity) else { throw HostError("Launch settings must not alias disk, EFI variables or serial log") }
        }
    }
    func applying(to settings: LaunchSettings, limits: LaunchLimits, log: (String) -> Void) -> LaunchSettings {
        var result = settings
        if let mb = memoryMB { result.memoryMB = mb }
        if let mode = screen { result.screen = mode }
        return result.validated(limits,log:log)
    }
    func showsLaunchMenu(_ settings: LaunchSettings, optionHeld: Bool) -> Bool {
        launchCapture != nil || (!(startImmediately || control != nil) && (settings.showAtStart || optionHeld))
    }
}

// Coordinate with both QEMU's POSIX byte-range locks and our image builder.
// Keep the builder-sidecar lock: VZ may open/close additional disk FDs, which
// can release process-owned POSIX locks on the disk itself.
final class DiskLease {
    private var descriptors: [Int32] = []
    init(_ image: URL) throws {
        do {
            let sidecar = image.deletingLastPathComponent().appendingPathComponent(".image-update.lock")
            for (path, flags) in [(sidecar.path, O_RDWR | O_CREAT), (image.path, O_RDWR)] {
                let fd = Darwin.open(path, flags | O_CLOEXEC, mode_t(0o600))
                guard fd >= 0 else { throw HostError("Cannot open \(path): \(String(cString: strerror(errno)))") }
                descriptors.append(fd)
                var lock = Darwin.flock()
                lock.l_type = Int16(F_WRLCK); lock.l_whence = Int16(SEEK_SET)
                guard fcntl(fd, F_SETLK, &lock) == 0 else {
                    throw HostError("Disk or image builder is busy. Shut down its VM first; no files were replaced.")
                }
            }
        } catch { release(); throw error }
    }
    func release() { for fd in descriptors { Darwin.close(fd) }; descriptors.removeAll() }
    deinit { release() }
}

let usageToMac: [Int: UInt16] = [
    4:0,5:11,6:8,7:2,8:14,9:3,10:5,11:4,12:34,13:38,14:40,15:37,
    16:46,17:45,18:31,19:35,20:12,21:15,22:1,23:17,24:32,25:9,
    26:13,27:7,28:16,29:6,30:18,31:19,32:20,33:21,34:23,35:22,
    36:26,37:28,38:25,39:29,40:36,41:53,42:51,43:48,44:49,
    45:27,46:24,47:33,48:30,49:42,51:41,52:39,53:50,54:43,55:47,56:44,
    57:57,58:122,59:120,60:99,61:118,62:96,63:97,64:98,65:100,
    66:101,67:109,68:103,69:111,73:114,74:115,75:116,76:117,
    77:119,78:121,79:124,80:123,81:125,82:126,
    224:59,225:56,226:58,227:55,228:62,229:60,230:61,231:54
]
func textKey(_ char: Character) -> (Int, Bool)? {
    let plain = Array("abcdefghijklmnopqrstuvwxyz1234567890\n\u{1b}\u{8}\t -=[]\\;'`,./")
    let shifted = Array("ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n\u{1b}\u{8}\t _+{}|:\"~<>?")
    let usages = Array(4...39) + [40,41,42,43,44,45,46,47,48,49,51,52,53,54,55,56]
    if let index = plain.firstIndex(of: char) { return (usages[index], false) }
    if let index = shifted.firstIndex(of: char) { return (usages[index], true) }
    return nil
}

// An explicitly controlled test view must not mix real desktop tracking
// events with scripted input. Normal interactive launches keep all input.
final class GuestView: VZVirtualMachineView {
    var controlOnly = false
    var injecting = false
    private var acceptsInput: Bool { !controlOnly || injecting }
    override func mouseMoved(with event: NSEvent) { if acceptsInput { super.mouseMoved(with:event) } }
    override func mouseEntered(with event: NSEvent) { if acceptsInput { super.mouseEntered(with:event) } }
    override func mouseExited(with event: NSEvent) { if acceptsInput { super.mouseExited(with:event) } }
    override func mouseDown(with event: NSEvent) { if acceptsInput { super.mouseDown(with:event) } }
    override func mouseUp(with event: NSEvent) { if acceptsInput { super.mouseUp(with:event) } }
    override func mouseDragged(with event: NSEvent) { if acceptsInput { super.mouseDragged(with:event) } }
    override func rightMouseDown(with event: NSEvent) { if acceptsInput { super.rightMouseDown(with:event) } }
    override func rightMouseUp(with event: NSEvent) { if acceptsInput { super.rightMouseUp(with:event) } }
    override func rightMouseDragged(with event: NSEvent) { if acceptsInput { super.rightMouseDragged(with:event) } }
    override func otherMouseDown(with event: NSEvent) { if acceptsInput { super.otherMouseDown(with:event) } }
    override func otherMouseUp(with event: NSEvent) { if acceptsInput { super.otherMouseUp(with:event) } }
    override func scrollWheel(with event: NSEvent) { if acceptsInput { super.scrollWheel(with:event) } }
    override func keyDown(with event: NSEvent) { if acceptsInput { super.keyDown(with:event) } }
    override func keyUp(with event: NSEvent) { if acceptsInput { super.keyUp(with:event) } }
    override func flagsChanged(with event: NSEvent) { if acceptsInput { super.flagsChanged(with:event) } }
}

final class VMHost: NSObject, NSApplicationDelegate, NSWindowDelegate, VZVirtualMachineDelegate {
    let options: Options
    var window: NSWindow!
    let view = GuestView(frame: .zero)
    var machine: VZVirtualMachine?
    private(set) var runningConfiguration: VZVirtualMachineConfiguration?
    var lease: DiskLease?
    var serialHandle: FileHandle?
    var controlServer: ControlServer?
    var held: Set<Int> = []
    let limits = LaunchLimits()
    var settings = LaunchSettings()
    var activeSettings = LaunchSettings()
    var launchWindow: LaunchWindow?
    var isActive: Bool { machine != nil && machine?.state != .stopped && machine?.state != .error }
    func logSettings(_ message: String) { fputs("ojjyOS: \(message)\n",stderr) }
    /* Full-screen transitions: styleMask flips before the Space animation
     * settles and AppKit silently drops toggleFullScreen calls made while
     * one is in flight (measured: windowed-size captures while "full
     * screen"). The delegate callbacks are the only completion signal. */
    var fullscreenBusy = 0, fullscreenStage = 0
    var fullscreenEventAt: TimeInterval = 0
    var startupFullscreenTimer: Timer?

    /* A transition can be dropped mid-flight (will... without did...),
     * which would leave the busy count stuck forever. Nothing is actually
     * animating that long after the last callback, so clear the stale
     * count instead of poisoning every later toggle. Main thread only. */
    func fullscreenQuiesce(_ stale: TimeInterval) {
        if fullscreenBusy != 0, ProcessInfo.processInfo.systemUptime - fullscreenEventAt > stale {
            fullscreenBusy = 0
        }
    }
    init(_ options: Options) { self.options = options }
    func applicationDidFinishLaunching(_ notification: Notification) {
        settings = LaunchSettings.load(options.settingsURL,limits:limits,log:logSettings)
        if let appearance = options.appearance { NSApp.appearance = NSAppearance(named:appearance == "dark" ? .darkAqua : .aqua) }
        makeMenus()
        window = NSWindow(contentRect: NSRect(x:0,y:0,width:1120,height:700),
            styleMask:[.titled,.closable,.miniaturizable,.resizable], backing:.buffered, defer:false)
        window.title = "ojjyOS"; window.minSize = NSSize(width:960,height:600)
        window.contentAspectRatio = NSSize(width:1440,height:900)
        // Document-window show animations temporarily scale the captured
        // Metal surface. Tests own visibility explicitly, so disable only
        // this host animation in control mode (guest motion stays enabled).
        if options.control != nil { window.animationBehavior = .none }
        window.collectionBehavior.insert(.fullScreenPrimary)
        window.contentView = view; window.delegate = self
        view.capturesSystemKeys = true
        view.controlOnly = options.control != nil
        // The guest currently uses a fixed Retina mode. The view scales it
        // to the window; automatic mode changes await guest GPU resize support.
        window.center()
        do {
            if let path = options.control { controlServer = try ControlServer(path, host:self) }
            if options.showsLaunchMenu(settings,optionHeld:NSEvent.modifierFlags.contains(.option)) {
                launchSettings()
                if let path = options.launchCapture {
                    DispatchQueue.main.asyncAfter(deadline:.now()+0.8) { [self] in
                        #if OJJY_VMAPP_UI_TEST
                        do {
                            if let panel = launchWindow { try runLaunchWindowTests(panel) }
                            try runLaunchFailureTests(self)
                            captureLaunchWindow(path + ".error.png") { [self] in
                                launchSettings()
                                captureLaunchWindow(path) { NSApp.terminate(nil) }
                            }
                        }
                        catch { fputs("ojjyOS: \(error)\n",stderr); exit(1) }
                        #else
                        captureLaunchWindow(path) { NSApp.terminate(nil) }
                        #endif
                    }
                }
            } else { try start() }
        } catch { fail(error) }
        #if OJJY_VMAPP_LAUNCH_TEST
        beginLaunchIntegration(self)
        #endif
    }
    func captureLaunchWindow(_ path: String, completion: @escaping () -> Void) {
        let deadline = ProcessInfo.processInfo.systemUptime + 3
        Timer.scheduledTimer(withTimeInterval:1.0/60,repeats:true) { [weak self] timer in
            do {
                guard let self = self else { timer.invalidate(); return }
                if try self.launchWindow?.capture(path) == true { timer.invalidate(); completion(); return }
                guard ProcessInfo.processInfo.systemUptime < deadline else { throw HostError("Launch window surface remained transparent; no screenshot saved") }
            } catch { fputs("ojjyOS: \(error)\n",stderr); exit(1) }
        }
    }
    func makeMenus() {
        let bar = NSMenu()
        let appItem = NSMenuItem(); bar.addItem(appItem)
        let appMenu = NSMenu(); appItem.submenu = appMenu
        appMenu.addItem(withTitle:"About ojjyOS",action:#selector(about),keyEquivalent:"").target = self
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle:"Quit ojjyOS",action:#selector(NSApplication.terminate(_:)),keyEquivalent:"q")
        let vmItem = NSMenuItem(); bar.addItem(vmItem); let vmMenu = NSMenu(title:"VM"); vmItem.submenu = vmMenu
        for (title, selector) in [("Start",#selector(startMenu)),("Request Stop",#selector(requestStop)),("Force Stop…",#selector(forceStop))] {
            vmMenu.addItem(withTitle:title,action:selector,keyEquivalent:"").target = self
        }
        vmMenu.addItem(.separator())
        vmMenu.addItem(withTitle:"Launch Settings…",action:#selector(launchSettings),keyEquivalent:",").target = self
        let viewItem = NSMenuItem(); bar.addItem(viewItem); let menu = NSMenu(title:"View"); viewItem.submenu = menu
        menu.addItem(withTitle:"Enter Full Screen",action:#selector(fullScreen),keyEquivalent:"f").target = self
        menu.items.last?.keyEquivalentModifierMask = [.control,.command]
        NSApp.mainMenu = bar
    }
    @objc func about() {
        NSApp.orderFrontStandardAboutPanel(options:[.applicationName:"ojjyOS",.applicationVersion:"3.1.0",
            .credits:NSAttributedString(string:"Created by Jonas Lee\nA custom kernel, running natively on Apple Virtualization.framework.")])
    }
    @objc func fullScreen() {
        startupFullscreenTimer?.invalidate(); startupFullscreenTimer = nil
        window.toggleFullScreen(nil)
    }
    func applyStartupFullscreen(_ desired: Bool) {
        startupFullscreenTimer?.invalidate()
        let deadline = ProcessInfo.processInfo.systemUptime + 12
        var lastAttempt: TimeInterval = 0, attempts = 0
        // Starting VZ can finish during AppKit's window-show transition.
        // A single toggle then emits willEnter but never didEnter (measured
        // busy=1/stage=0 for 45s). Use the existing control-path completion
        // rule and stale recovery asynchronously; never block AppKit's queue.
        startupFullscreenTimer = Timer.scheduledTimer(withTimeInterval:0.1,repeats:true) { [weak self] timer in
            guard let self = self, self.machine?.state == .running else { timer.invalidate(); return }
            let now = ProcessInfo.processInfo.systemUptime
            self.fullscreenQuiesce(2.5)
            if self.fullscreenBusy == 0 && self.window.styleMask.contains(.fullScreen) == desired {
                timer.invalidate(); return
            }
            if now > deadline {
                self.logSettings("macOS did not complete startup full screen; use View → Enter Full Screen to retry")
                timer.invalidate(); return
            }
            if self.fullscreenBusy == 0 && attempts < 3 && now-lastAttempt > 2 {
                self.window.toggleFullScreen(nil); attempts += 1; lastAttempt = now
                self.logSettings("Startup full-screen request \(attempts)")
            }
        }
    }
    @objc func launchSettings() {
        if launchWindow == nil {
            let panel = LaunchWindow(limits:limits)
            panel.isActive = { [weak self] in self?.isActive ?? false }
            panel.onApply = { [weak self] selected in
                guard let self = self else { return }
                try selected.save(self.options.settingsURL)
                self.settings = selected
                self.logSettings("Launch settings saved; changes apply on next start")
                if !self.isActive { try self.start() }
            }
            launchWindow = panel
        }
        launchWindow?.show(settings)
    }
    @objc func startMenu() { do { try start() } catch { fail(error) } }
    func start() throws {
        guard machine == nil || machine?.state == .stopped || machine?.state == .error else {
            throw HostError("The virtual machine is already active")
        }
        guard !limits.memoryChoices.isEmpty else { throw HostError("No supported RAM setting within half this Mac's memory") }
        activeSettings = options.applying(to:settings,limits:limits,log:logSettings)
        let display = activeSettings.display
        window.contentAspectRatio = NSSize(width:display.width,height:display.height)
        if !window.styleMask.contains(.fullScreen) {
            window.setContentSize(NSSize(width:1120,height:1120/display.aspect))
        }
        window.makeKeyAndOrderFront(nil); window.makeFirstResponder(view)
        NSApp.activate(ignoringOtherApps:true)
        view.virtualMachine = nil; machine = nil; serialHandle = nil; lease = nil; held.removeAll()
        lease = try DiskLease(options.image)
        let cfg = VZVirtualMachineConfiguration()
        cfg.platform = VZGenericPlatformConfiguration(); cfg.cpuCount = 1
        cfg.memorySize = UInt64(activeSettings.memoryMB) * 1024 * 1024
        let boot = VZEFIBootLoader()
        boot.variableStore = FileManager.default.fileExists(atPath:options.variables.path)
            ? VZEFIVariableStore(url:options.variables)
            : try VZEFIVariableStore(creatingVariableStoreAt:options.variables)
        cfg.bootLoader = boot
        let disk = try VZDiskImageStorageDeviceAttachment(url:options.image,readOnly:false)
        cfg.storageDevices = [VZVirtioBlockDeviceConfiguration(attachment:disk)]
        guard FileManager.default.createFile(atPath:options.serial.path,contents:nil) else { throw HostError("Cannot create serial log") }
        serialHandle = try FileHandle(forWritingTo:options.serial)
        let console = VZVirtioConsoleDeviceSerialPortConfiguration()
        console.attachment = VZFileHandleSerialPortAttachment(fileHandleForReading:nil,fileHandleForWriting:serialHandle!)
        cfg.serialPorts = [console]
        let gpu = VZVirtioGraphicsDeviceConfiguration()
        gpu.scanouts = [VZVirtioGraphicsScanoutConfiguration(widthInPixels:display.width*2,heightInPixels:display.height*2)]
        cfg.graphicsDevices = [gpu]
        cfg.keyboards = [VZUSBKeyboardConfiguration()]
        cfg.pointingDevices = [VZUSBScreenCoordinatePointingDeviceConfiguration()]
        cfg.entropyDevices = [VZVirtioEntropyDeviceConfiguration()]
        let network = VZVirtioNetworkDeviceConfiguration(); network.attachment = VZNATNetworkDeviceAttachment()
        cfg.networkDevices = activeSettings.network ? [network] : []
        logSettings("Starting with \(activeSettings.memoryMB) MB, \(display.id) Retina, network \(activeSettings.network ? "on" : "off")")
        try cfg.validate()
        runningConfiguration = cfg
        let vm = VZVirtualMachine(configuration:cfg); vm.delegate = self
        machine = vm; view.virtualMachine = vm
        window.title = "ojjyOS · Starting"
        vm.start { [weak self] result in
            switch result {
            case .success:
                guard let self = self else { return }
                self.window.title = "ojjyOS"; self.launchWindow?.window.orderOut(nil)
                if self.activeSettings.fullScreen != self.window.styleMask.contains(.fullScreen) {
                    self.applyStartupFullscreen(self.activeSettings.fullScreen)
                }
                print("VM started")
            case .failure(let error): self?.startFailed(error)
            }
        }
    }
    @objc func requestStop() {
        do { guard let vm = machine else { return }; try vm.requestStop() }
        catch { fail(HostError("Use Shut Down inside ojjyOS, then Quit. Request Stop: \(error)")) }
    }
    @objc func forceStop() {
        guard let vm = machine, vm.canStop else { return }
        let alert = NSAlert(); alert.messageText = "Force stop ojjyOS?"
        alert.informativeText = "Unsaved guest changes will be lost. Shut down inside ojjyOS whenever possible."
        alert.addButton(withTitle:"Cancel"); alert.addButton(withTitle:"Force Stop")
        if alert.runModal() == .alertSecondButtonReturn { vm.stop { [weak self] error in if let error = error { self?.fail(error) } } }
    }
    func guestDidStop(_ virtualMachine: VZVirtualMachine) {
        window.title = "ojjyOS · Stopped"; launchWindow?.refreshState(); print("Guest stopped")
    }
    func virtualMachine(_ virtualMachine: VZVirtualMachine,didStopWithError error: Error) { fail(error) }
    func fail(_ error: Error) {
        window.title = "ojjyOS · \(error)"
        fputs("ojjyOS: \(error)\n",stderr)
    }
    func startFailed(_ error: Error) {
        fail(error)
        // Async VZ startup errors arrive after Start closed the menu.
        // Restore a retry surface for interactive launches, never automation.
        if options.control == nil && !options.startImmediately {
            launchSettings()
            launchWindow?.notice.stringValue = "Could not start ojjyOS: \(error). Adjust settings and try Start again."
        }
    }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        if let vm = machine, vm.state != .stopped && vm.state != .error { forceStop(); return .terminateCancel }
        return .terminateNow
    }
    func applicationWillTerminate(_ notification: Notification) { startupFullscreenTimer?.invalidate(); controlServer?.close() }
    func windowShouldClose(_ sender: NSWindow) -> Bool { NSApp.terminate(nil); return false }
    func windowWillEnterFullScreen(_ notification: Notification) { fullscreenBusy += 1; fullscreenEventAt = ProcessInfo.processInfo.systemUptime }
    func windowDidEnterFullScreen(_ notification: Notification) { fullscreenBusy -= 1; fullscreenStage += 1; fullscreenEventAt = ProcessInfo.processInfo.systemUptime }
    func windowWillExitFullScreen(_ notification: Notification) { fullscreenBusy += 1; fullscreenEventAt = ProcessInfo.processInfo.systemUptime }
    func windowDidExitFullScreen(_ notification: Notification) { fullscreenBusy -= 1; fullscreenStage += 1; fullscreenEventAt = ProcessInfo.processInfo.systemUptime }
    var flags: NSEvent.ModifierFlags {
        var result: NSEvent.ModifierFlags = []
        for (usages,flag) in [([224,228],NSEvent.ModifierFlags.control),([225,229],.shift),([226,230],.option),([227,231],.command)] {
            if usages.contains(where:held.contains) { result.insert(flag) }
        }
        // flagsChanged carries both the aggregate flags and the physical
        // left/right NX_DEVICE* bits. VZ uses the latter for USB modifiers.
        let deviceBits: [Int:UInt] = [224:0x1,225:0x2,226:0x20,227:0x8,
                                     228:0x2000,229:0x4,230:0x40,231:0x10]
        for usage in held { if let bit = deviceBits[usage] { result.formUnion(NSEvent.ModifierFlags(rawValue:bit)) } }
        return result
    }
    func key(_ usage: Int,down: Bool) throws {
        guard machine?.state == .running, let code = usageToMac[usage] else { throw HostError("VM not running or unsupported HID usage") }
        if down { held.insert(usage) } else { held.remove(usage) }
        let type: NSEvent.EventType = usage >= 224 ? .flagsChanged : (down ? .keyDown : .keyUp)
        guard let event = NSEvent.keyEvent(with:type,location:.zero,modifierFlags:flags,
            timestamp:ProcessInfo.processInfo.systemUptime,windowNumber:window.windowNumber,context:nil,
            characters:"",charactersIgnoringModifiers:"",isARepeat:false,keyCode:code) else { throw HostError("Cannot form key event") }
        view.injecting = true
        defer { view.injecting = false }
        if type == .flagsChanged { view.flagsChanged(with:event) }
        else if down { view.keyDown(with:event) } else { view.keyUp(with:event) }
    }
    func mouse(_ x: Double,_ y: Double,type: NSEvent.EventType) throws {
        let display = activeSettings.display
        guard machine?.state == .running, x.isFinite,y.isFinite,x>=0,y>=0,x<Double(display.width),y<Double(display.height) else {
            throw HostError("Coordinates must be within \(display.width)×\(display.height) logical pixels")
        }
        let bounds = view.bounds
        let width = min(bounds.width,bounds.height*display.aspect), height = width/display.aspect
        let top = (bounds.height-height)/2+y*height/Double(display.height)
        let point = NSPoint(x:(bounds.width-width)/2+x*width/Double(display.width),y:view.isFlipped ? top : bounds.height-top)
        let location = view.convert(point,to:nil)
        guard let event = NSEvent.mouseEvent(with:type,location:location,modifierFlags:flags,
            timestamp:ProcessInfo.processInfo.systemUptime,windowNumber:window.windowNumber,context:nil,
            eventNumber:0,clickCount:type == .mouseMoved ? 0 : 1,pressure:type == .leftMouseDown ? 1 : 0) else { throw HostError("Cannot form mouse event") }
        view.injecting = true
        defer { view.injecting = false }
        switch type { case .leftMouseDown: view.mouseDown(with:event); case .leftMouseUp: view.mouseUp(with:event); default: view.mouseMoved(with:event) }
    }
    func screenshot(_ path: String) throws -> Bool {
        guard let image = CGWindowListCreateImage(.null,.optionIncludingWindow,CGWindowID(window.windowNumber),.boundsIgnoreFraming) else { return false }
        let rect = window.convertToScreen(view.convert(view.bounds,to:nil))
        let sx = Double(image.width)/window.frame.width, sy = Double(image.height)/window.frame.height
        let crop = CGRect(x:(rect.minX-window.frame.minX)*sx,y:(window.frame.maxY-rect.maxY)*sy,width:rect.width*sx,height:rect.height*sy)
        guard let content = image.cropping(to:crop) else { return false }
        let bitmap = NSBitmapImageRep(cgImage:content)
        // WindowServer can return a fully transparent surface, or an
        // internally scaled show-animation surface with transparent edges.
        // Neither is a completed view capture. Ignore rounded corners, but
        // require all four edge midpoints to be opaque before publishing.
        let w = bitmap.pixelsWide, h = bitmap.pixelsHigh
        guard w > 10, h > 10 else { return false }
        for (x,y) in [(4,h/2),(w-5,h/2),(w/2,4),(w/2,h-5)] {
            guard let color = bitmap.colorAt(x:x,y:y),color.alphaComponent >= 0.99 else { return false }
        }
        guard let png = bitmap.representation(using:.png,properties:[:]) else { throw HostError("Own-window encoding failed") }
        let url = URL(fileURLWithPath:path)
        guard !FileManager.default.fileExists(atPath:url.path) else { throw HostError("Screenshot target already exists") }
        try png.write(to:url,options:.atomic)
        return true
    }
    func prepareScreenshot() {
        // Metal surfaces can be uncomposited when their window is occluded.
        // Front the window ONLY when it is not already visible, key and
        // active: unconditional makeKeyAndOrderFront/activate during a
        // settling full-screen Space fires stray transition callbacks and
        // gets later toggles dropped (measured on this host).
        if !window.isVisible || !window.isKeyWindow || !NSApp.isActive {
            window.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
        }
        window.displayIfNeeded()
        view.displayIfNeeded()
    }
}

// Enabled only by --control. Owner-only socket, bounded lines, no shell or
// global event injection. All events go straight to this VM's own view.
final class ControlServer {
    let path: String
    let fd: Int32
    weak var host: VMHost?
    var inode: UInt64 = 0
    init(_ path: String,host: VMHost) throws {
        self.path = path; self.host = host
        guard path.utf8.count < 104 else { throw HostError("Control socket path is too long") }
        fd = socket(AF_UNIX,SOCK_STREAM,0)
        guard fd >= 0 else { throw HostError("Cannot create control socket") }
        var address = sockaddr_un(); address.sun_family = sa_family_t(AF_UNIX)
        address.sun_len = UInt8(MemoryLayout<sockaddr_un>.size)
        path.withCString { source in withUnsafeMutablePointer(to:&address.sun_path) {
            $0.withMemoryRebound(to:CChar.self,capacity:104) { _ = strlcpy($0,source,104) }
        } }
        let previousMask = umask(0o077)
        let result = withUnsafePointer(to:&address) { $0.withMemoryRebound(to:sockaddr.self,capacity:1) {
            Darwin.bind(fd,$0,socklen_t(MemoryLayout<sockaddr_un>.size))
        } }
        umask(previousMask)
        guard result == 0 else { Darwin.close(fd); throw HostError("Cannot bind control socket; existing paths are never removed") }
        var info = stat(); if lstat(path,&info) == 0 { inode = UInt64(info.st_ino) }
        guard chmod(path,0o600) == 0, listen(fd,4) == 0 else { close(); throw HostError("Cannot secure/listen on control socket") }
        signal(SIGPIPE,SIG_IGN)
        DispatchQueue.global(qos:.userInitiated).async { [weak self] in self?.serve() }
    }
    func close() {
        Darwin.shutdown(fd,SHUT_RDWR); Darwin.close(fd)
        var info = stat()
        if lstat(path,&info) == 0 && UInt64(info.st_ino) == inode { _ = unlink(path) }
    }
    func onMain<T>(_ body: @escaping (VMHost) throws -> T) throws -> T {
        try DispatchQueue.main.sync { guard let host = self.host else { throw HostError("Host closed") }; return try body(host) }
    }
    func command(_ line: String) throws -> [String:Any] {
        if line.hasPrefix("text ") {
            let text = String(line.dropFirst(5))
            let keys = text.map(textKey)
            guard !keys.contains(where: { $0 == nil }) else { throw HostError("Text injection currently supports US ASCII only") }
            for entry in keys {
                let (usage,shift) = entry!
                if shift { try onMain { try $0.key(225,down:true) }; usleep(3000) }
                try onMain { try $0.key(usage,down:true) }; usleep(5000)
                try onMain { try $0.key(usage,down:false) }; usleep(5000)
                if shift { try onMain { try $0.key(225,down:false) }; usleep(3000) }
            }
            return ["ok":true]
        }
        if line.hasPrefix("screenshot ") {
            try onMain { $0.prepareScreenshot() }
            // Wait for observable completion, not an arbitrary sleep. No
            // PNG is written until the view's surface is opaque. A genuine
            // opaque black guest frame still fails the caller's pixel gate.
            let deadline = ProcessInfo.processInfo.systemUptime + 3
            while try !onMain({ host in try autoreleasepool { try host.screenshot(String(line.dropFirst(11))) } }) {
                guard ProcessInfo.processInfo.systemUptime < deadline else { throw HostError("Own-window surface remained transparent; no screenshot saved") }
                usleep(16667)
            }
            return ["ok":true]
        }
        let parts = line.split(separator:" ").map(String.init)
        if parts.count == 3 && parts[0] == "key",let usage = Int(parts[1]),let down = Int(parts[2]),(down == 0 || down == 1) {
            try onMain { try $0.key(usage,down:down == 1) }; usleep(4000); return ["ok":true]
        }
        if parts.count == 3 && (parts[0] == "move" || parts[0] == "click"),let x = Double(parts[1]),let y = Double(parts[2]) {
            try onMain { try $0.mouse(x,y,type:.mouseMoved) }; usleep(4000)
            if parts[0] == "click" {
                try onMain { try $0.mouse(x,y,type:.leftMouseDown) }; usleep(5000)
                try onMain { try $0.mouse(x,y,type:.leftMouseUp) }; usleep(5000)
            }
            return ["ok":true]
        }
        if line == "status" { return try onMain { ["ok":true,"state":$0.machine?.state.rawValue ?? -1,
            "fullscreen":$0.window.styleMask.contains(.fullScreen),
            "visible":$0.window.isVisible,
            "viewWidth":$0.view.bounds.width,"viewHeight":$0.view.bounds.height,
            "memoryMB":$0.activeSettings.memoryMB,"screen":$0.activeSettings.screen,
            "network":$0.activeSettings.network,"pendingMemoryMB":$0.settings.memoryMB] } }
        if line == "hide" { try onMain { $0.window.orderOut(nil) }; return ["ok":true] }
        if line == "fullscreen" {
            // Settle any in-flight Space animation first: a toggle issued
            // mid-transition is dropped without error. Then flip and wait
            // for the delegate's completion callback, re-issuing once if
            // macOS ignored the request. styleMask alone is not proof.
            let settleEnd = ProcessInfo.processInfo.systemUptime + 6
            while try onMain({ host in host.fullscreenQuiesce(2.5); return host.fullscreenBusy > 0 }) {
                if ProcessInfo.processInfo.systemUptime > settleEnd { throw HostError("previous full-screen transition did not settle") }
                usleep(100000)
            }
            let before = try onMain { $0.fullscreenStage }
            try onMain { $0.fullScreen() }
            let toggledAt = ProcessInfo.processInfo.systemUptime
            let completeEnd = toggledAt + 10
            var attempts = 0
            while true {
                if try onMain({ host in host.fullscreenQuiesce(2.5); return host.fullscreenStage > before }) { break }
                if ProcessInfo.processInfo.systemUptime > completeEnd { throw HostError("macOS did not complete the full-screen transition") }
                // Re-issue periodically: a toggle made during a settling
                // animation is dropped without error, and re-issuing while
                // a real transition runs is harmless.
                if attempts < 3, ProcessInfo.processInfo.systemUptime > toggledAt + Double(attempts + 1) * 2.0 {
                    try onMain { $0.fullScreen() }
                    attempts += 1
                }
                usleep(100000)
            }
            return ["ok":true]
        }
        if line == "start" { try onMain { try $0.start() }; return ["ok":true] }
        if line == "quit" {
            try onMain {
                guard $0.machine == nil || $0.machine?.state == .stopped || $0.machine?.state == .error else { throw HostError("Shut down the guest before quit") }
                DispatchQueue.main.asyncAfter(deadline:.now()+0.1) { NSApp.terminate(nil) }
            }
            return ["ok":true]
        }
        throw HostError("Unknown control command")
    }
    func serve() {
        while true {
            let client = accept(fd,nil,nil); if client < 0 { return }
            var timeout = timeval(tv_sec:15,tv_usec:0)
            _ = setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,socklen_t(MemoryLayout<timeval>.size))
            var data = Data(),byte: UInt8 = 0
            while data.count <= 16384 && Darwin.read(client,&byte,1) == 1 {
                if byte == 10 { break }; data.append(byte)
            }
            let response: [String:Any]
            do {
                guard data.count <= 16384,byte == 10,let line = String(data:data,encoding:.utf8) else { throw HostError("Invalid or oversized control line") }
                response = try command(line)
            } catch { response = ["ok":false,"error":"\(error)"] }
            if let json = try? JSONSerialization.data(withJSONObject:response) {
                var result = json; result.append(10)
                result.withUnsafeBytes { bytes in
                    var sent = 0
                    while sent < bytes.count {
                        let n = Darwin.write(client,bytes.baseAddress!.advanced(by:sent),bytes.count-sent)
                        if n <= 0 { break }; sent += n
                    }
                }
            }
            Darwin.close(client)
        }
    }
}

#if OJJY_VMAPP_TEST
do { try runVMHostTests() } catch { fputs("VM host tests failed: \(error)\n",stderr); exit(1) }
#else
do {
    #if OJJY_VMAPP_LAUNCH_TEST
    guard let testBuild = ProcessInfo.processInfo.environment["OJJY_LAUNCH_TEST_OUTPUT"] else { throw HostError("Private test output required") }
    let options = try Options(build:URL(fileURLWithPath:testBuild))
    #else
    let options = try Options()
    #endif
    let delegate = VMHost(options)
    let app = NSApplication.shared
    app.setActivationPolicy(.regular); app.delegate = delegate
    withExtendedLifetime(delegate) { app.run() }
} catch { fputs("ojjyOS: \(error)\n",stderr); exit(1) }
#endif
