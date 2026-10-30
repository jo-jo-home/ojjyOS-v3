// Native launch/preferences surface. Created by Jonas Lee.
import AppKit

// launch-2 removes this constant once HW-13 makes high RAM usable.

final class LaunchWindow: NSObject, NSWindowDelegate {
    let window: NSWindow
    let memory = NSPopUpButton()
    let screen = NSPopUpButton()
    let network = NSButton(checkboxWithTitle:"Enable network (NAT)",target:nil,action:nil)
    let fullScreen = NSButton(checkboxWithTitle:"Start in full screen",target:nil,action:nil)
    let showAtStart = NSButton(checkboxWithTitle:"Show this menu when ojjyOS opens",target:nil,action:nil)
    let primary = NSButton(title:"Start",target:nil,action:nil)
    let notice = NSTextField(wrappingLabelWithString:"")
    let limits: LaunchLimits
    var onApply: ((LaunchSettings) throws -> Void)?
    var isActive: (() -> Bool)?

    init(limits: LaunchLimits) {
        self.limits = limits
        window = NSWindow(contentRect:NSRect(x:0,y:0,width:600,height:590),
            styleMask:[.titled,.closable,.fullSizeContentView],backing:.buffered,defer:false)
        super.init()
        window.title = "ojjyOS · Launch Settings"; window.titleVisibility = .hidden
        window.titlebarAppearsTransparent = true; window.isReleasedWhenClosed = false
        window.delegate = self
        window.backgroundColor = NSColor(name:"LaunchBlue") { appearance in
            appearance.bestMatch(from:[.aqua,.darkAqua]) == .darkAqua
                ? NSColor(srgbRed:0.04,green:0.10,blue:0.19,alpha:1)
                : NSColor(srgbRed:0.89,green:0.94,blue:1,alpha:1)
        }
        let content = NSView(); window.contentView = content
        let glass = NSVisualEffectView()
        glass.material = .sidebar; glass.blendingMode = .withinWindow; glass.state = .active
        glass.translatesAutoresizingMaskIntoConstraints = false; content.addSubview(glass)
        NSLayoutConstraint.activate([
            glass.leadingAnchor.constraint(equalTo:content.leadingAnchor),glass.trailingAnchor.constraint(equalTo:content.trailingAnchor),
            glass.topAnchor.constraint(equalTo:content.topAnchor),glass.bottomAnchor.constraint(equalTo:content.bottomAnchor)])
        // Controls sit above (not inside) the effect so vibrancy never turns
        // checkbox labels white on the light material in own-window captures.
        let stack = NSStackView(); stack.orientation = .vertical; stack.alignment = .leading; stack.spacing = 14
        stack.translatesAutoresizingMaskIntoConstraints = false; content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo:glass.leadingAnchor,constant:32),
            stack.trailingAnchor.constraint(equalTo:glass.trailingAnchor,constant:-32),
            stack.topAnchor.constraint(equalTo:glass.topAnchor,constant:46),
            stack.bottomAnchor.constraint(lessThanOrEqualTo:glass.bottomAnchor,constant:-24)])
        let title = NSTextField(labelWithString:"ojjyOS")
        title.font = .systemFont(ofSize:34,weight:.bold); title.textColor = .systemBlue
        stack.addArrangedSubview(title)
        let credit = NSTextField(labelWithString:"Created by Jonas Lee")
        credit.font = .systemFont(ofSize:13,weight:.medium); stack.addArrangedSubview(credit)
        stack.setCustomSpacing(22,after:credit)
        notice.font = .systemFont(ofSize:13); notice.textColor = .labelColor
        stack.addArrangedSubview(notice)
        notice.widthAnchor.constraint(equalTo:stack.widthAnchor).isActive = true
        for mb in limits.memoryChoices { memory.addItem(withTitle:"\(mb / 1024) GB"); memory.lastItem?.tag = mb }
        for mode in LaunchScreen.supported { screen.addItem(withTitle:mode.label) }
        row("Memory",memory,stack:stack)
        let cpu = NSPopUpButton(); cpu.addItem(withTitle:"1 core"); cpu.isEnabled = false
        row("CPU cores",cpu,stack:stack)
        note("More cores come with SMP-01",stack:stack)
        row("Screen size",screen,stack:stack)
        note("Retina uses 2× pixels. Screen size changes after shutdown.",stack:stack)
        let divider = NSBox(); divider.boxType = .separator
        stack.addArrangedSubview(divider); divider.widthAnchor.constraint(equalTo:stack.widthAnchor).isActive = true
        for button in [network,fullScreen,showAtStart] {
            stack.addArrangedSubview(button)
        }
        note("Hold Option while opening to bring this menu back.",stack:stack)
        let actions = NSStackView(); actions.orientation = .horizontal; actions.spacing = 10
        let quit = NSButton(title:"Quit",target:self,action:#selector(quitApp))
        let restore = NSButton(title:"Restore Defaults",target:self,action:#selector(restoreDefaults))
        let spacer = NSView()
        for button in [quit,restore,primary] { button.bezelStyle = .rounded }
        primary.target = self; primary.action = #selector(apply); primary.keyEquivalent = "\r"
        primary.bezelColor = .systemBlue; primary.contentTintColor = .white
        primary.widthAnchor.constraint(greaterThanOrEqualToConstant:90).isActive = true
        for item in [quit,restore,spacer,primary] { actions.addArrangedSubview(item) }
        spacer.setContentHuggingPriority(.defaultLow,for:.horizontal)
        stack.addArrangedSubview(actions); actions.widthAnchor.constraint(equalTo:stack.widthAnchor).isActive = true
        stack.setCustomSpacing(22,after:stack.arrangedSubviews[stack.arrangedSubviews.count-2])
        // Fit real text/control heights rather than clipping at a hand-picked height.
        content.layoutSubtreeIfNeeded()
        window.setContentSize(NSSize(width:600,height:stack.fittingSize.height+72))
        window.center()
    }
    private func row(_ title: String,_ control: NSView,stack: NSStackView) {
        let label = NSTextField(labelWithString:title); label.font = .systemFont(ofSize:13,weight:.medium)
        label.widthAnchor.constraint(equalToConstant:104).isActive = true
        control.widthAnchor.constraint(equalToConstant:270).isActive = true
        control.setAccessibilityLabel(title)
        let row = NSStackView(views:[label,control]); row.orientation = .horizontal; row.spacing = 16
        row.alignment = .centerY; stack.addArrangedSubview(row)
    }
    private func note(_ text: String,stack: NSStackView) {
        let label = NSTextField(wrappingLabelWithString:text)
        label.font = .systemFont(ofSize:11); label.textColor = .secondaryLabelColor
        stack.addArrangedSubview(label); label.widthAnchor.constraint(equalTo:stack.widthAnchor).isActive = true
        stack.setCustomSpacing(8,after:stack.arrangedSubviews[stack.arrangedSubviews.count-2])
    }
    func populate(_ settings: LaunchSettings) {
        memory.selectItem(withTag:settings.memoryMB)
        screen.selectItem(at:LaunchScreen.supported.firstIndex(of:settings.display) ?? 0)
        network.state = settings.network ? .on : .off
        fullScreen.state = settings.fullScreen ? .on : .off
        showAtStart.state = settings.showAtStart ? .on : .off
        refreshState()
    }
    func refreshState() {
        let active = isActive?() ?? false
        notice.stringValue = active ? "Changes apply after you shut down ojjyOS" : "Choose how ojjyOS starts."
        primary.title = active ? "Save" : "Start"
        primary.isEnabled = !limits.memoryChoices.isEmpty
        if limits.memoryChoices.isEmpty { notice.stringValue = "This Mac has no supported RAM setting within half its memory. A host with more RAM is required." }
    }
    func selection() -> LaunchSettings {
        var value = LaunchSettings()
        value.memoryMB = memory.selectedItem?.tag ?? 2048
        value.screen = LaunchScreen.supported[max(0,screen.indexOfSelectedItem)].id
        value.network = network.state == .on; value.fullScreen = fullScreen.state == .on
        value.showAtStart = showAtStart.state == .on
        return value
    }
    func show(_ settings: LaunchSettings) {
        populate(settings); window.makeKeyAndOrderFront(nil); NSApp.activate(ignoringOtherApps:true)
    }
    @objc func restoreDefaults() { populate(LaunchSettings().validated(limits,log:{ _ in })) }
    @objc func apply() {
        do { try onApply?(selection()); window.orderOut(nil) }
        catch { notice.stringValue = "Could not save or start: \(error). Try again or Quit." }
    }
    @objc func quitApp() { NSApp.terminate(nil) }
    func windowShouldClose(_ sender: NSWindow) -> Bool {
        if isActive?() == true { window.orderOut(nil) } else { NSApp.terminate(nil) }
        return false
    }
    func capture(_ path: String) throws -> Bool {
        window.displayIfNeeded()
        guard let image = CGWindowListCreateImage(.null,.optionIncludingWindow,CGWindowID(window.windowNumber),.boundsIgnoreFraming) else { return false }
        let bitmap = NSBitmapImageRep(cgImage:image)
        let w = bitmap.pixelsWide, h = bitmap.pixelsHigh
        guard w > 10, h > 10 else { return false }
        for (x,y) in [(4,h/2),(w-5,h/2),(w/2,4),(w/2,h-5)] {
            guard let color = bitmap.colorAt(x:x,y:y), color.alphaComponent >= 0.99 else { return false }
        }
        guard let png = bitmap.representation(using:.png,properties:[:]) else { throw HostError("Cannot encode launch window") }
        guard !FileManager.default.fileExists(atPath:path) else { throw HostError("Screenshot target already exists") }
        try png.write(to:URL(fileURLWithPath:path),options:.atomic)
        return true
    }
}
