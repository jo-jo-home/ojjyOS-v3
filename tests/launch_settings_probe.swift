// ojjyOS - Created by Jonas Lee
import Foundation

@main struct LaunchSettingsProbe {
    static func main() {
        do {
            let args = Array(CommandLine.arguments.dropFirst())
            guard args.count >= 3, let hostBytes = UInt64(args[2]) else { throw HostError("operation path hostBytes [JSON revision] required") }
            let url = URL(fileURLWithPath:args[1])
            let limits = LaunchLimits(minimum:512 << 20,maximum:64 << 30,hostMemory:hostBytes)
            guard !limits.memoryChoices.isEmpty else { throw HostError("No supported host RAM choice") }
            if args[0] == "save" {
                guard args.count == 5 else { throw HostError("save requires JSON and revision") }
                let value = LaunchSettings.decode(Data(args[3].utf8),limits:limits,log:{ _ in })
                try value.save(url,expectedRevision:args[4])
            } else if args[0] != "load" { throw HostError("Unknown operation") }
            let snapshot = LaunchSettings.snapshot(url,limits:limits,log:{ _ in })
            let object: [String:Any] = ["settings":try JSONSerialization.jsonObject(with:snapshot.settings.encoded()),
                                      "revision":snapshot.revision]
            let output = try JSONSerialization.data(withJSONObject:object,options:[.sortedKeys])
            print(String(decoding:output,as:UTF8.self))
        } catch { fputs("Launch settings probe: \(error)\n",stderr); exit(1) }
    }
}
