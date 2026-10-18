// Feasibility probe: boot the ojjyOS disk with Apple's Virtualization
// framework (UEFI), headless, serial console -> file. Created by Jonas Lee.
import Foundation
import Virtualization

let args = CommandLine.arguments
let disk = URL(fileURLWithPath: args[1])
let vars = URL(fileURLWithPath: args[2])
let log = URL(fileURLWithPath: args[3])
let seconds = Double(args.count > 4 ? args[4] : "20") ?? 20

let cfg = VZVirtualMachineConfiguration()
cfg.platform = VZGenericPlatformConfiguration()
cfg.cpuCount = 1
cfg.memorySize = 2 << 30
let store = FileManager.default.fileExists(atPath: vars.path)
    ? VZEFIVariableStore(url: vars)
    : try! VZEFIVariableStore(creatingVariableStoreAt: vars)
let boot = VZEFIBootLoader()
boot.variableStore = store
cfg.bootLoader = boot
let att = try! VZDiskImageStorageDeviceAttachment(url: disk, readOnly: false)
cfg.storageDevices = [VZVirtioBlockDeviceConfiguration(attachment: att)]
FileManager.default.createFile(atPath: log.path, contents: nil)
let serial = VZVirtioConsoleDeviceSerialPortConfiguration()
serial.attachment = VZFileHandleSerialPortAttachment(fileHandleForReading: nil,
    fileHandleForWriting: try! FileHandle(forWritingTo: log))
cfg.serialPorts = [serial]
let gpu = VZVirtioGraphicsDeviceConfiguration()
gpu.scanouts = [VZVirtioGraphicsScanoutConfiguration(widthInPixels: 1440, heightInPixels: 900)]
cfg.graphicsDevices = [gpu]
cfg.keyboards = [VZUSBKeyboardConfiguration()]
cfg.pointingDevices = [VZUSBScreenCoordinatePointingDeviceConfiguration()]
let net = VZVirtioNetworkDeviceConfiguration()
net.attachment = VZNATNetworkDeviceAttachment()
cfg.networkDevices = [net]
cfg.entropyDevices = [VZVirtioEntropyDeviceConfiguration()]
do { try cfg.validate() } catch { print("invalid: \(error)"); exit(1) }

let vm = VZVirtualMachine(configuration: cfg)
vm.start { r in
    if case .failure(let e) = r { print("start failed: \(e)"); exit(2) }
    print("started")
}
let bootTime = Date()
Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { t in
    if vm.state == .stopped || vm.state == .error {
        print(String(format: "stopped-after %.2fs state %d",
                     Date().timeIntervalSince(bootTime), vm.state.rawValue))
        exit(0)
    }
}
DispatchQueue.main.asyncAfter(deadline: .now() + seconds) {
    print("still-running state \(vm.state.rawValue)"); exit(0)
}
RunLoop.main.run()
