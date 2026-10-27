// Private-disk VZ boot/device probe. Created by Jonas Lee.
import Foundation
import Virtualization

final class ProbeDelegate: NSObject, VZVirtualMachineDelegate {
    func virtualMachine(_ virtualMachine: VZVirtualMachine, didStopWithError error: Error) {
        print("VM failed: \(error)"); exit(3)
    }
}

@main struct BootProbe {
    static func main() throws {
        var args = Array(CommandLine.arguments.dropFirst())
        guard args.count >= 3 else { throw HostError("Usage: probe disk vars log [seconds] [--memory MB] [--screen WxH]") }
        let disk = URL(fileURLWithPath:args.removeFirst())
        let vars = URL(fileURLWithPath:args.removeFirst())
        let log = URL(fileURLWithPath:args.removeFirst())
        var seconds: Double = 20
        if let first = args.first, !first.hasPrefix("--") {
            guard let value = Double(args.removeFirst()), value.isFinite, value > 0 else { throw HostError("Invalid duration") }
            seconds = value
        }
        var settings = LaunchSettings()
        while !args.isEmpty {
            let flag = args.removeFirst()
            guard !args.isEmpty else { throw HostError("Missing value for \(flag)") }
            let value = args.removeFirst()
            if flag == "--memory", let mb = Int(value), mb > 0 { settings.memoryMB = mb }
            else if flag == "--screen", LaunchScreen.parse(value) != nil { settings.screen = value }
            else { throw HostError("Invalid probe option \(flag): \(value)") }
        }
        let limits = LaunchLimits()
        guard limits.memoryChoices.contains(settings.memoryMB) else { throw HostError("Unsupported RAM for this host") }
        let cfg = VZVirtualMachineConfiguration()
        cfg.platform = VZGenericPlatformConfiguration(); cfg.cpuCount = 1
        cfg.memorySize = UInt64(settings.memoryMB) * 1024 * 1024
        let store = FileManager.default.fileExists(atPath:vars.path)
            ? VZEFIVariableStore(url:vars) : try VZEFIVariableStore(creatingVariableStoreAt:vars)
        let boot = VZEFIBootLoader(); boot.variableStore = store; cfg.bootLoader = boot
        let att = try VZDiskImageStorageDeviceAttachment(url:disk,readOnly:false)
        cfg.storageDevices = [VZVirtioBlockDeviceConfiguration(attachment:att)]
        guard FileManager.default.createFile(atPath:log.path,contents:nil) else { throw HostError("Cannot create probe serial log") }
        let serial = VZVirtioConsoleDeviceSerialPortConfiguration()
        serial.attachment = VZFileHandleSerialPortAttachment(fileHandleForReading:nil,fileHandleForWriting:try FileHandle(forWritingTo:log))
        cfg.serialPorts = [serial]
        let gpu = VZVirtioGraphicsDeviceConfiguration(), display = settings.display
        gpu.scanouts = [VZVirtioGraphicsScanoutConfiguration(widthInPixels:display.width*2,heightInPixels:display.height*2)]
        cfg.graphicsDevices = [gpu]
        cfg.keyboards = [VZUSBKeyboardConfiguration()]
        cfg.pointingDevices = [VZUSBScreenCoordinatePointingDeviceConfiguration()]
        let net = VZVirtioNetworkDeviceConfiguration(); net.attachment = VZNATNetworkDeviceAttachment()
        cfg.networkDevices = [net]; cfg.entropyDevices = [VZVirtioEntropyDeviceConfiguration()]
        try cfg.validate()
        let delegate = ProbeDelegate(), vm = VZVirtualMachine(configuration:cfg)
        vm.delegate = delegate
        vm.start { result in
            if case .failure(let error) = result { print("start failed: \(error)"); exit(2) }
            print("started \(settings.memoryMB) MB \(display.id) Retina")
        }
        let bootTime = Date()
        Timer.scheduledTimer(withTimeInterval:0.05,repeats:true) { _ in
            if vm.state == .stopped || vm.state == .error {
                print(String(format:"stopped-after %.2fs state %d",Date().timeIntervalSince(bootTime),vm.state.rawValue))
                exit(vm.state == .error ? 3 : 0)
            }
        }
        DispatchQueue.main.asyncAfter(deadline:.now()+seconds) { print("still-running state \(vm.state.rawValue)"); exit(0) }
        withExtendedLifetime(delegate) { RunLoop.main.run() }
    }
}
