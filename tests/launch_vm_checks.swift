// Live launch lifecycle checks, private fixtures only. Created by Jonas Lee.
import AppKit
import Virtualization

func beginLaunchIntegration(_ host: VMHost) {
    host.view.controlOnly = true  // isolate our view-local scripted pointer
    let output = URL(fileURLWithPath:ProcessInfo.processInfo.environment["OJJY_LAUNCH_TEST_OUTPUT"]!)
    let deadline = ProcessInfo.processInfo.systemUptime + 45
    var phase = 0, phaseAt = ProcessInfo.processInfo.systemUptime
    var checks = 0
    func check(_ value: Bool,_ name: String) throws {
        checks += 1
        guard value else { throw HostError("Live launch check: \(name)") }
    }
    func tick(_ timer: Timer) throws {
            guard ProcessInfo.processInfo.systemUptime < deadline else {
                throw HostError("Live launch deadline (phase \(phase), VM=\(host.machine?.state.rawValue ?? -1), fullscreen=\(host.window.styleMask.contains(.fullScreen)), busy=\(host.fullscreenBusy), stage=\(host.fullscreenStage))")
            }
            switch phase {
            case 0:
                let menuExpected = host.options.showsLaunchMenu(host.settings,optionHeld:false)
                if menuExpected {
                    try check(host.machine == nil && host.lease == nil && host.launchWindow?.window.isVisible == true,"menu before VM/disk lease")
                    host.launchWindow!.primary.performClick(nil)
                } else {
                    try check(host.launchWindow == nil,"automated/direct startup bypasses menu")
                }
                phase = 1
            case 1:
                let serial = (try? String(contentsOf:host.options.serial,encoding:.utf8)) ?? ""
                guard host.machine?.state == .running, serial.contains("[TIMER] Health"), host.fullscreenBusy == 0 else { return }
                let active = host.activeSettings
                let cfg = host.runningConfiguration!
                let expectedMemory = UInt64(active.memoryMB) * 1024 * 1024
                try check(cfg.memorySize == expectedMemory,"RAM applied")
                try check(cfg.networkDevices.count == (active.network ? 1 : 0),"network on/off applied")
                let gpu = cfg.graphicsDevices[0] as! VZVirtioGraphicsDeviceConfiguration
                try check(gpu.scanouts[0].widthInPixels == active.display.width*2 && gpu.scanouts[0].heightInPixels == active.display.height*2,"Retina scanout")
                try check(host.window.contentAspectRatio == NSSize(width:active.display.width,height:active.display.height),"window aspect follows screen")
                try check(host.window.styleMask.contains(.fullScreen) == active.fullScreen,"start full screen setting")
                host.launchSettings()
                phase = 2
            case 2:
                let active = host.activeSettings, vm = host.machine!
                let panel = host.launchWindow!
                try check(panel.primary.title == "Save" && panel.notice.stringValue == "Changes apply after you shut down ojjyOS","real running save-only state")
                guard try panel.capture(output.appendingPathComponent("running-settings.png").path) else { return }
                panel.memory.selectItem(withTag:4096); panel.screen.selectItem(at:0)
                panel.network.state = active.network ? .off : .on
                panel.primary.performClick(nil)
                try check(host.machine === vm && host.activeSettings == active && host.settings.memoryMB == 4096,"running changes are pending only")
                try check(LaunchSettings.load(host.options.settingsURL,limits:host.limits,log:{ _ in }) == host.settings,"pending file round trip")
                phase = 3
            case 3:
                host.prepareScreenshot()
                guard try host.screenshot(output.appendingPathComponent("screen.png").path) else { return }
                try host.mouse(100,100,type:.mouseMoved)
                phase = 4; phaseAt = ProcessInfo.processInfo.systemUptime
            case 4:
                guard ProcessInfo.processInfo.systemUptime - phaseAt > 0.3 else { return }
                guard try host.screenshot(output.appendingPathComponent("pointer.png").path) else { return }
                let display = host.activeSettings.display
                try host.mouse(Double(display.width-1),Double(display.height-1),type:.mouseMoved)
                for (x,y) in [(Double(display.width),0.0),(0.0,Double(display.height)),(-1.0,0.0),(Double.nan,0.0)] {
                    var rejected = false
                    do { try host.mouse(x,y,type:.mouseMoved) } catch { rejected = true }
                    try check(rejected,"screen-relative coordinate bounds")
                }
                let result: [String:Any] = ["checks":checks,"screen":display.id,"memoryMB":host.activeSettings.memoryMB,
                    "network":host.activeSettings.network,"fullScreen":host.activeSettings.fullScreen,"pendingMemoryMB":host.settings.memoryMB]
                try JSONSerialization.data(withJSONObject:result,options:.prettyPrinted).write(to:output.appendingPathComponent("lifecycle.json"))
                timer.invalidate()
                host.machine!.stop { error in
                    if let error = error { fputs("Private VM stop: \(error)\n",stderr); exit(1) }
                    print("PASS: \(checks) live launch lifecycle checks"); exit(0)
                }
            default: break
            }
    }
    Timer.scheduledTimer(withTimeInterval:0.1,repeats:true) { timer in
        do { try tick(timer) }
        catch { fputs("\(error)\n",stderr); exit(1) }
    }
}
