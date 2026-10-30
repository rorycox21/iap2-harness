// Experimental Classic Bluetooth accessory transport. SDP registration is not
// proof that macOS emits the CarPlay EIR. Inspect on-air discovery separately.
import Foundation
import IOBluetooth
import Darwin

func log(_ text: String) { FileHandle.standardError.write(Data(("bt-accessory: " + text + "\n").utf8)) }
func number(_ value: Int, _ size: Int) -> [String: Any] {
    ["DataElementType": 1, "DataElementSize": size, "DataElementValue": value]
}
func uuid(_ value: String) -> Data {
    let hex = value.replacingOccurrences(of: "-", with: "")
    return Data(stride(from: 0, to: hex.count, by: 2).map {
        let a = hex.index(hex.startIndex, offsetBy: $0)
        return UInt8(hex[a..<hex.index(a, offsetBy: 2)], radix: 16)!
    })
}
func record(_ name: String, _ service: String, _ preferredChannel: Int) -> [String: Any] {
    ["0001 - ServiceClassIDList": [uuid(service)],
     "0004 - ProtocolDescriptorList": [[uuid("0100")], [uuid("0003"), number(preferredChannel, 1)]],
     "0005 - BrowseGroupList": [uuid("1002")],
     "0009 - BluetoothProfileDescriptorList": [[uuid("1101"), number(0x0100, 2)]],
     "0100 - ServiceName": name,
     "LocalAttributes": ["Persistent": false]]
}
func normalized(_ value: String) -> String { value.lowercased().replacingOccurrences(of: "-", with: ":") }

final class Accessory: NSObject, IOBluetoothRFCOMMChannelDelegate {
    var records: [IOBluetoothSDPServiceRecord] = []
    var notifications: [IOBluetoothUserNotification] = []
    var sources: [DispatchSourceSignal] = []
    var channel: IOBluetoothRFCOMMChannel?
    var peer: String?
    var originalClass: BluetoothClassOfDevice?
    var heartbeat: Timer?
    var cleaned = false

    func stop(_ code: Int32) -> Never {
        if !cleaned {
            cleaned = true
            heartbeat?.invalidate()
            FileHandle.standardInput.readabilityHandler = nil
            channel?.setDelegate(nil)
            channel?.close()
            notifications.forEach { $0.unregister() }
            for r in records { log("remove SDP status=\(r.remove())") }
            if let old = originalClass, let host = IOBluetoothHostController.default() {
                log("restore class status=\(host.setClassOfDevice(old, forTimeInterval: 30))")
            }
            log("stopped; temporary registrations removed")
        }
        exit(code)
    }

    func start(peer: String?, seconds: Double, carClass: Bool) {
        self.peer = peer.map(normalized)
        for sig in [SIGINT, SIGTERM] {
            signal(sig, SIG_IGN)
            let source = DispatchSource.makeSignalSource(signal: sig, queue: .main)
            source.setEventHandler { self.stop(0) }
            source.resume()
            sources.append(source)
        }
        signal(SIGPIPE, SIG_IGN)
        guard let host = IOBluetoothHostController.default() else { log("no controller"); stop(1) }
        log("controller=\(host.addressAsString() ?? "unknown") name=\(host.nameAsString() ?? "unknown") class=\(String(host.classOfDevice(), radix: 16))")
        var assignedChannels = Set<BluetoothRFCOMMChannelID>()
        for (name, service, preferred) in [("iAP2", "00000000-deca-fade-deca-deafdecacaff", 1),
                                           ("CarPlay", "ec884348-cd41-40a2-9727-575d50bf1fd3", 2)] {
            guard let published = IOBluetoothSDPServiceRecord.publishedServiceRecord(with: record(name, service, preferred)) else {
                log("failed to publish \(name)"); stop(1)
            }
            records.append(published)
            var id: BluetoothRFCOMMChannelID = 0
            var handle: BluetoothSDPServiceRecordHandle = 0
            let channelStatus = published.getRFCOMMChannelID(&id)
            let handleStatus = published.getHandle(&handle)
            guard channelStatus == kIOReturnSuccess, id > 0, handleStatus == kIOReturnSuccess else {
                log("invalid assigned channel/record for \(name): channel=\(id) status=\(channelStatus) handleStatus=\(handleStatus)"); stop(1)
            }
            guard assignedChannels.insert(id).inserted else { log("duplicate assigned channel"); stop(1) }
            guard let notification = IOBluetoothRFCOMMChannel.register(forChannelOpenNotifications: self,
                selector: #selector(incoming(_:channel:)), withChannelID: id,
                direction: kIOBluetoothUserNotificationChannelDirectionIncoming) else {
                log("failed to register incoming \(name) channel"); stop(1)
            }
            notifications.append(notification)
            log("published \(name) uuid=\(service) handle=\(handle) channel=\(id)")
        }
        if carClass {
            let old = host.classOfDevice()
            let status = host.setClassOfDevice(0x200418, forTimeInterval: 120)
            log("temporary car-audio class request status=\(status), readback=\(String(host.classOfDevice(), radix: 16))")
            if status == kIOReturnSuccess { originalClass = old }
        }
        log("READY: local SDP and incoming callbacks registered. On-air EIR and discoverability UNVERIFIED.")
        log(peer == nil ? "advertisement probe only; incoming channels will be closed" : "waiting for incoming RFCOMM from selected phone; stdout carries raw bytes")
        heartbeat = Timer.scheduledTimer(withTimeInterval: seconds, repeats: false) { _ in
            log("session deadline reached"); self.stop(0)
        }
        RunLoop.main.run()
    }

    @objc func incoming(_ notification: IOBluetoothUserNotification, channel opened: IOBluetoothRFCOMMChannel) {
        let address = normalized(opened.getDevice().addressString ?? "")
        guard let peer = peer, address == peer, channel == nil else {
            log("incoming channel rejected (probe, unexpected peer, or session already active)")
            opened.close()
            return
        }
        guard opened.setDelegate(self) == kIOReturnSuccess else { opened.close(); log("delegate assignment failed"); stop(1) }
        channel = opened
        log("incoming RFCOMM accepted channel=\(opened.getID())")
        // Start reading only now: the harness can queue SYN before a phone connects.
        FileHandle.standardInput.readabilityHandler = { handle in
            let data = handle.availableData
            DispatchQueue.main.async { self.send(data) }
        }
    }

    func send(_ data: Data) {
        if data.isEmpty { log("stdin EOF"); stop(0) }
        guard let channel = channel else { return }
        let mtu = max(1, min(Int(channel.getMTU()), Int(UInt16.max)))
        var offset = 0
        while offset < data.count {
            let count = min(mtu, data.count - offset)
            var bytes = [UInt8](data[offset..<offset + count])
            let status = channel.writeSync(&bytes, length: UInt16(count))
            if status != kIOReturnSuccess { log("RFCOMM write status=\(status)"); stop(1) }
            offset += count
        }
    }
    func rfcommChannelData(_ sender: IOBluetoothRFCOMMChannel!, data pointer: UnsafeMutableRawPointer!, length count: Int) {
        FileHandle.standardOutput.write(Data(bytes: pointer, count: count))
    }
    func rfcommChannelClosed(_ sender: IOBluetoothRFCOMMChannel!) { log("RFCOMM closed"); stop(0) }
}

let args = CommandLine.arguments
func value(_ flag: String) -> String? {
    guard let i = args.firstIndex(of: flag), i + 1 < args.count else { return nil }
    return args[i + 1]
}
if args.contains("--help") {
    print("usage: bt-accessory --advertise [--seconds 5..120] [--car-class]\n       bt-accessory --address PHONE [--seconds 5..120] [--car-class]\n--advertise probes local SDP; --address accepts only that phone and bridges raw stdio.\nOpen macOS Bluetooth settings for system-managed discovery. EIR remains unverified.")
    exit(0)
}
let peer = value("--address")
let duration = Double(value("--seconds") ?? "120") ?? 0
let allowed = Set(["--advertise", "--address", "--seconds", "--car-class"])
var i = 1
while i < args.count {
    guard allowed.contains(args[i]) else { log("unknown option \(args[i])"); exit(2) }
    if args[i] == "--address" || args[i] == "--seconds" { i += 1; if i >= args.count { log("missing option value"); exit(2) } }
    i += 1
}
guard (peer != nil) != args.contains("--advertise"), duration >= 5, duration <= 120 else { log("choose --advertise or --address PHONE; seconds must be 5..120"); exit(2) }
if let peer = peer {
    guard peer.range(of: "^[0-9A-Fa-f]{2}([:-][0-9A-Fa-f]{2}){5}$", options: .regularExpression) != nil else { log("invalid phone address"); exit(2) }
}
Accessory().start(peer: peer, seconds: duration, carClass: args.contains("--car-class") || peer != nil)
