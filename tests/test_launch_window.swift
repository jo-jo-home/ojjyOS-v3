// AppKit-only interaction guards. No VM/disk is opened. Created by Jonas Lee.
import AppKit

func runLaunchWindowTests(_ panel: LaunchWindow) throws {
    var checks = 0
    func check(_ condition: Bool,_ name: String) throws {
        checks += 1; if !condition { throw HostError("Launch UI: \(name)") }
    }
    let oldApply = panel.onApply, oldActive = panel.isActive
    defer { panel.onApply = oldApply; panel.isActive = oldActive; panel.show(LaunchSettings()) }
    var running = false, applied: LaunchSettings?
    panel.isActive = { running }
    panel.onApply = { applied = $0 }
    panel.show(LaunchSettings())
    try check(panel.primary.title == "Start" && panel.primary.keyEquivalent == "\r","default Return action")
    panel.memory.selectItem(withTag:3072); panel.screen.selectItem(at:1)
    panel.network.state = .off; panel.fullScreen.state = .on; panel.showAtStart.state = .off
    panel.primary.performClick(nil)
    try check(applied?.memoryMB == 3072 && applied?.screen == "1280x800" && applied?.network == false && applied?.fullScreen == true && applied?.showAtStart == false,"Start reads controls")
    try check(!panel.window.isVisible,"Start hides launch window")
    running = true; panel.show(applied!)
    try check(panel.primary.title == "Save" && panel.notice.stringValue == "Changes apply after you shut down ojjyOS","running save-only label")
    panel.memory.selectItem(withTag:4096); panel.primary.performClick(nil)
    try check(applied?.memoryMB == 4096 && running,"Save changes pending settings, not running state")
    panel.show(applied!); panel.restoreDefaults()
    try check(panel.selection() == LaunchSettings(),"Restore Defaults restores every control")
    panel.onApply = { _ in throw HostError("fixture write failure") }
    panel.primary.performClick(nil)
    try check(panel.window.isVisible && panel.notice.stringValue.contains("fixture write failure"),"write failure stays visible/recoverable")
    print("PASS: \(checks) launch-window interaction checks (no VM)")
}

func runLaunchFailureTests(_ host: VMHost) throws {
    host.startFailed(HostError("Startup fixture failure"))
    guard host.machine == nil, host.launchWindow?.window.isVisible == true,
          host.launchWindow?.notice.stringValue.contains("Startup fixture failure") == true else {
        throw HostError("Async startup failure did not restore retry settings")
    }
    print("PASS: startup-failure retry surface (no VM)")
}
