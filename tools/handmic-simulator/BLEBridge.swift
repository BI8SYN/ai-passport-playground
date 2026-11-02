import AppKit
import CoreBluetooth
import Foundation

// All CoreBluetooth, WebSocket and queue state is confined to the main queue.
private func monoNS() -> UInt64 { DispatchTime.now().uptimeNanoseconds }
private func uuid(_ n: Int) -> CBUUID {
    CBUUID(string: String(format: "9c3a%04x-7a9b-4d9f-a15f-4d4f4c494e4b", n))
}
private func hexData(_ text: String) -> Data? {
    guard text.count % 2 == 0, text.count <= 1040 else { return nil }
    let chars = Array(text.utf8)
    func nibble(_ c: UInt8) -> UInt8? {
        switch c {
        case 48...57: return c - 48
        case 65...70: return c - 55
        case 97...102: return c - 87
        default: return nil
        }
    }
    var bytes = [UInt8]()
    for i in stride(from: 0, to: chars.count, by: 2) {
        guard let a = nibble(chars[i]), let b = nibble(chars[i + 1]) else { return nil }
        bytes.append(a * 16 + b)
    }
    return Data(bytes)
}
private func lowerHex(_ data: Data) -> String { data.map { String(format: "%02x", $0) }.joined() }

final class BLEBridge: NSObject, NSApplicationDelegate, CBPeripheralManagerDelegate,
                       URLSessionWebSocketDelegate, @unchecked Sendable {
    private var manager: CBPeripheralManager!
    private var chars: [String: CBMutableCharacteristic] = [:]
    private var servicesReady = false
    private var publishing = false
    private var advertiseEnabled = false
    private var advertisingPending = false
    private var central: CBCentral?
    private var subscriptions = Set<CBUUID>()
    private var blocked = false
    private var stopping = false
    private var socket: URLSessionWebSocketTask?
    private var socketReady = false
    private var reconnectDelay = 0.5
    private var reconnectWork: DispatchWorkItem?
    private lazy var session = URLSession(configuration: .ephemeral, delegate: self,
                                          delegateQueue: .main)
    private var outbound = [String]()
    private var sending = false
    private var timer: Timer?
    private var signals = [DispatchSourceSignal]()
    private var info = Data(#"{"product":"rf-link-handmic","device_id":"mac-simulator","fw":"1.3.0","major":[1],"minor":1,"formats":[2],"sample_rate":8000,"frame_samples":160,"audio_bytes":100,"credit_window":32,"control_bytes":512,"pairing":"os_managed_diagnostic"}"#.utf8)
    private var status = Data(#"{"simulator":true,"ready":false,"reason":"backend_starting"}"#.utf8)
    private struct Packet { let data: Data; let at: UInt64 }
    private struct Transfer {
        let id: UInt32
        let total: Int
        let at: UInt64
        var used = 0
        var fragments = [Data]()
    }
    private var assembling: Transfer?
    private var controls = [[Data]]()
    private var mic = [Packet]()
    private var flow: Data?
    private var droppedMic = 0
    private var droppedFlow = 0
    private var droppedControl = 0
    private var backpressure = 0
    private var rejectedWrites = 0

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApplication.shared.setActivationPolicy(.accessory)
        connectSocket()
        createManager()
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            self?.tick()
        }
        for signum in [SIGTERM, SIGINT] {
            signal(signum, SIG_IGN)
            let source = DispatchSource.makeSignalSource(signal: signum, queue: .main)
            source.setEventHandler { NSApplication.shared.terminate(nil) }
            source.resume()
            signals.append(source)
        }
    }

    func applicationWillTerminate(_ notification: Notification) {
        stopping = true
        timer?.invalidate()
        reconnectWork?.cancel()
        if manager.state == .poweredOn { manager.stopAdvertising(); manager.removeAllServices() }
        socket?.cancel(with: .goingAway, reason: nil)
        session.invalidateAndCancel()
        signals.forEach { $0.cancel() }
    }

    private func log(_ message: String) {
        FileHandle.standardError.write(Data(("[BLE bridge] " + message + "\n").utf8))
    }

    private func state(_ value: String, _ message: String) {
        log("\(value): \(message)")
        _ = send(["type": "bridge_state", "state": value, "message": message])
    }

    @discardableResult private func send(_ object: [String: Any]) -> Bool {
        guard socketReady, outbound.count < 256,
              let data = try? JSONSerialization.data(withJSONObject: object),
              let text = String(data: data, encoding: .utf8) else { return false }
        outbound.append(text)
        pumpSocket()
        return true
    }

    private func pumpSocket() {
        guard !sending, socketReady, let socket, let first = outbound.first else { return }
        sending = true
        socket.send(.string(first)) { [weak self, weak socket] error in
            DispatchQueue.main.async {
                guard let self, let socket, self.socket === socket else { return }
                self.sending = false
                if let error { self.socketFailed(socket, error); return }
                if !self.outbound.isEmpty { self.outbound.removeFirst() }
                self.pumpSocket()
            }
        }
    }

    private func connectSocket() {
        guard !stopping else { return }
        let task = session.webSocketTask(with: URL(string: "ws://127.0.0.1:8765/bridge")!)
        task.maximumMessageSize = 64 * 1024
        socket = task
        task.resume()
        receive(task)
    }

    func urlSession(_ session: URLSession, webSocketTask: URLSessionWebSocketTask,
                    didOpenWithProtocol protocol: String?) {
        guard webSocketTask === socket else { return }
        socketReady = true
        reconnectDelay = 0.5
        state(managerState(), "local bridge connected; encryption/pairing controlled by macOS")
        reportLink()
        updateAdvertising()
    }

    func urlSession(_ session: URLSession, webSocketTask: URLSessionWebSocketTask,
                    didCloseWith closeCode: URLSessionWebSocketTask.CloseCode, reason: Data?) {
        socketFailed(webSocketTask, NSError(domain: "LocalBridge", code: Int(closeCode.rawValue)))
    }

    private func receive(_ task: URLSessionWebSocketTask) {
        task.receive { [weak self, weak task] result in
            DispatchQueue.main.async {
                guard let self, let task, self.socket === task else { return }
                switch result {
                case .success(let message):
                    let data: Data
                    switch message {
                    case .data(let value): data = value
                    case .string(let value): data = Data(value.utf8)
                    @unknown default: self.receive(task); return
                    }
                    if let value = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
                        self.command(value)
                    } else { self.state("error", "invalid bridge JSON") }
                    self.receive(task)
                case .failure(let error): self.socketFailed(task, error)
                }
            }
        }
    }

    private func socketFailed(_ task: URLSessionWebSocketTask, _ error: Error) {
        guard task === socket, !stopping else { return }
        socketReady = false
        socket = nil
        task.cancel(with: .goingAway, reason: nil)
        sending = false
        outbound.removeAll()
        advertiseEnabled = false // Require a fresh backend authorization after reconnect.
        log("local bridge disconnected: \(error.localizedDescription)")
        resetManager()
        let work = DispatchWorkItem { [weak self] in self?.connectSocket() }
        reconnectWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + reconnectDelay, execute: work)
        reconnectDelay = min(reconnectDelay * 2, 10)
    }

    private func command(_ object: [String: Any]) {
        switch object["type"] as? String {
        case "characteristic":
            guard let channel = object["channel"] as? String,
                  let text = object["text"] as? String, text.utf8.count <= 512,
                  let parsed = try? JSONSerialization.jsonObject(with: Data(text.utf8)),
                  parsed is [String: Any] else { state("error", "invalid Info/Status snapshot"); return }
            if channel == "info" { info = Data(text.utf8) }
            else if channel == "status" { status = Data(text.utf8) }
        case "advertise":
            advertiseEnabled = object["enabled"] as? Bool ?? false
            updateAdvertising()
        case "reset_link": resetManager()
        case "ble_notify":
            guard let channel = object["channel"] as? String,
                  let hex = object["hex"] as? String, let data = hexData(hex) else {
                state("error", "invalid notification bytes"); return
            }
            enqueue(channel, data)
        default: break
        }
    }

    private func createManager() {
        manager = CBPeripheralManager(delegate: self, queue: .main,
                                      options: [CBPeripheralManagerOptionShowPowerAlertKey: false])
    }

    private func resetManager() {
        clearLink()
        if let manager, manager.state == .poweredOn {
            manager.stopAdvertising()
            manager.removeAllServices()
        }
        manager?.delegate = nil
        servicesReady = false
        publishing = false
        advertisingPending = false
        chars.removeAll()
        createManager()
        state("poweredOff", "virtual GATT session reset; OS bonds and physical device untouched")
    }

    private func managerState() -> String {
        guard let manager else { return "poweredOff" }
        switch manager.state {
        case .poweredOn: return manager.isAdvertising ? "advertising" : "poweredOn"
        case .poweredOff, .resetting, .unknown: return "poweredOff"
        case .unauthorized: return "unauthorized"
        case .unsupported: return "unsupported"
        @unknown default: return "error"
        }
    }

    func peripheralManagerDidUpdateState(_ peripheral: CBPeripheralManager) {
        guard peripheral === manager else { return }
        state(managerState(), "CoreBluetooth state changed")
        if peripheral.state == .poweredOn {
            if !servicesReady && !publishing { publishServices() }
            else { updateAdvertising() }
        } else {
            clearLink()
            advertisingPending = false
            // CoreBluetooth retains services when merely poweredOff; other states clear the DB.
            if peripheral.state != .poweredOff { servicesReady = false; publishing = false; chars.removeAll() }
        }
    }

    private func publishServices() {
        guard manager.state == .poweredOn else { return }
        let specs: [(String, Int, CBCharacteristicProperties, CBAttributePermissions)] = [
            ("info", 2, [.read], [.readable]),
            ("control_rx", 3, [.write], [.writeable, .writeEncryptionRequired]),
            ("control", 4, [.indicate, .indicateEncryptionRequired], []),
            ("mic", 5, [.notify, .notifyEncryptionRequired], []),
            ("speaker", 6, [.writeWithoutResponse], [.writeable, .writeEncryptionRequired]),
            ("flow", 7, [.notify, .notifyEncryptionRequired], []),
            ("app_flow", 8, [.writeWithoutResponse], [.writeable, .writeEncryptionRequired]),
            ("status", 9, [.read], [.readable, .readEncryptionRequired]),
        ]
        chars.removeAll()
        var values = [CBMutableCharacteristic]()
        for (channel, number, properties, permissions) in specs {
            let value = CBMutableCharacteristic(type: uuid(number), properties: properties,
                                                value: nil, permissions: permissions)
            chars[channel] = value
            values.append(value)
        }
        let service = CBMutableService(type: uuid(1), primary: true)
        service.characteristics = values
        publishing = true
        manager.add(service)
    }

    func peripheralManager(_ peripheral: CBPeripheralManager, didAdd service: CBService, error: Error?) {
        guard peripheral === manager else { return }
        publishing = false
        if let error { state("error", "GATT publish: \(error.localizedDescription)"); return }
        servicesReady = true
        updateAdvertising()
    }

    private func updateAdvertising() {
        guard let manager, manager.state == .poweredOn else { return }
        if !advertiseEnabled || !socketReady || central != nil {
            manager.stopAdvertising()
            advertisingPending = false
            return
        }
        guard servicesReady, !manager.isAdvertising, !advertisingPending else { return }
        advertisingPending = true
        manager.startAdvertising([CBAdvertisementDataLocalNameKey: "RF Link · SIM",
                                  CBAdvertisementDataServiceUUIDsKey: [uuid(1)]])
    }

    func peripheralManagerDidStartAdvertising(_ peripheral: CBPeripheralManager, error: Error?) {
        guard peripheral === manager else { return }
        advertisingPending = false
        if let error { state("error", "advertising: \(error.localizedDescription)") }
        else if !advertiseEnabled || !socketReady || central != nil { updateAdvertising() }
        else { state("advertising", "RF Link · SIM; diagnostic peripheral, OS-managed encryption") }
    }

    private var fullySubscribed: Bool {
        [uuid(4), uuid(5), uuid(7)].allSatisfy { subscriptions.contains($0) }
    }

    private func reportLink(disconnectedID: String? = nil) {
        _ = send(["type": "link", "connected": central != nil,
                  "central_id": central?.identifier.uuidString ?? disconnectedID ?? "",
                  "max_value": min(244, central?.maximumUpdateValueLength ?? 20),
                  "subscribed": fullySubscribed])
    }

    private func clearQueues() {
        assembling = nil
        controls.removeAll()
        mic.removeAll()
        flow = nil
        blocked = false
    }

    private func clearLink() {
        let id = central?.identifier.uuidString
        central = nil
        subscriptions.removeAll()
        clearQueues()
        reportLink(disconnectedID: id)
    }

    func peripheralManager(_ peripheral: CBPeripheralManager, central peer: CBCentral,
                           didSubscribeTo characteristic: CBCharacteristic) {
        guard peripheral === manager else { return }
        guard [uuid(4), uuid(5), uuid(7)].contains(characteristic.uuid) else { return }
        guard socketReady, central == nil || central?.identifier == peer.identifier else {
            state("error", "subscription rejected for additional central / unavailable backend")
            return // Public API has no per-central ATT disconnect; never send data to this peer.
        }
        if central == nil {
            central = peer
            manager.stopAdvertising()
            manager.setDesiredConnectionLatency(.low, for: peer)
            log("virtual central \(peer.identifier.uuidString), max update \(peer.maximumUpdateValueLength)")
        }
        subscriptions.insert(characteristic.uuid)
        reportLink()
        drain()
    }

    func peripheralManager(_ peripheral: CBPeripheralManager, central peer: CBCentral,
                           didUnsubscribeFrom characteristic: CBCharacteristic) {
        guard peripheral === manager, central?.identifier == peer.identifier else { return }
        subscriptions.remove(characteristic.uuid)
        clearQueues() // No payload from an earlier subscription generation may survive.
        if subscriptions.isEmpty { clearLink(); updateAdvertising() }
        else { reportLink() }
    }

    func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveRead request: CBATTRequest) {
        guard peripheral === manager else { return }
        let value: Data
        if request.characteristic.uuid == uuid(2) { value = info }
        else if request.characteristic.uuid == uuid(9) {
            if let central, central.identifier != request.central.identifier {
                peripheral.respond(to: request, withResult: .insufficientAuthorization); return
            }
            value = status
        } else { peripheral.respond(to: request, withResult: .readNotPermitted); return }
        guard request.offset >= 0, request.offset <= value.count else {
            peripheral.respond(to: request, withResult: .invalidOffset); return
        }
        request.value = value.subdata(in: request.offset..<value.count)
        peripheral.respond(to: request, withResult: .success)
    }

    func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveWrite requests: [CBATTRequest]) {
        guard peripheral === manager, let first = requests.first else { return }
        let arrival = monoNS()
        var messages = [[String: Any]]()
        var failure: CBATTError.Code?
        // Validate the whole delegate batch before forwarding any ATT value.
        for request in requests {
            guard socketReady, outbound.count + requests.count < 256 else { failure = .unlikelyError; break }
            guard let central, central.identifier == request.central.identifier, fullySubscribed else {
                failure = .insufficientAuthorization; break
            }
            guard request.offset == 0 else { failure = .invalidOffset; break }
            let channel: String
            switch request.characteristic.uuid {
            case uuid(3): channel = "control"
            case uuid(6): channel = "speaker"
            case uuid(8): channel = "app_flow"
            default: failure = .writeNotPermitted; continue
            }
            guard let data = request.value, !data.isEmpty, data.count <= 244,
                  (channel != "speaker" || data.count == 100),
                  (channel != "app_flow" || data.count == 24),
                  (channel != "control" || data.count > 8) else {
                failure = .invalidAttributeValueLength; break
            }
            messages.append(["type": "ble_write", "channel": channel,
                             "hex": lowerHex(data), "arrival_ns": arrival])
        }
        if let failure {
            rejectedWrites += requests.count
            peripheral.respond(to: first, withResult: failure)
            if failure == .unlikelyError { state("error", "ATT write rejected: local bridge unavailable/full") }
            return
        }
        for message in messages { _ = send(message) }
        // This ACK only means the local diagnostic bridge accepted the ATT batch.
        peripheral.respond(to: first, withResult: .success)
    }

    private func enqueue(_ channel: String, _ data: Data) {
        guard central != nil, fullySubscribed else { return }
        guard data.count <= min(244, central!.maximumUpdateValueLength) else {
            state("error", "\(channel) value exceeds central maximum; refusing truncation")
            resetManager(); return
        }
        switch channel {
        case "control":
            guard data.count > 8 else { controlFailure("short control fragment"); return }
            let bytes = [UInt8](data)
            let id = UInt32(bytes[0]) | UInt32(bytes[1]) << 8 | UInt32(bytes[2]) << 16 | UInt32(bytes[3]) << 24
            let offset = Int(bytes[4]) | Int(bytes[5]) << 8
            let total = Int(bytes[6]) | Int(bytes[7]) << 8
            if assembling == nil {
                guard offset == 0, total > 0, total <= 512 else { controlFailure("invalid first fragment"); return }
                assembling = Transfer(id: id, total: total, at: monoNS())
            }
            guard var transfer = assembling, transfer.id == id, transfer.total == total,
                  transfer.used == offset, offset + data.count - 8 <= total else {
                controlFailure("interleaved/noncontiguous control fragments"); return
            }
            transfer.used += data.count - 8
            transfer.fragments.append(data)
            if transfer.used == total {
                guard controls.count < 16 else { controlFailure("control queue full"); return }
                controls.append(transfer.fragments)
                assembling = nil
            } else { assembling = transfer }
        case "mic":
            guard data.count == 100 else { state("error", "invalid mic packet length"); return }
            expireMic()
            if mic.count >= 6 { mic.removeFirst(); droppedMic += 1 }
            mic.append(Packet(data: data, at: monoNS()))
        case "flow":
            guard data.count == 24 else { state("error", "invalid device-flow packet length"); return }
            if flow != nil { droppedFlow += 1 }
            flow = data
        default: return
        }
        drain()
    }

    private func controlFailure(_ message: String) {
        droppedControl += 1
        state("error", message + "; resetting virtual session")
        resetManager()
    }

    private func expireMic() {
        let now = monoNS()
        while let first = mic.first, now - first.at >= 120_000_000 {
            mic.removeFirst(); droppedMic += 1
        }
    }

    private func drain() {
        guard !blocked, socketReady, fullySubscribed, manager.state == .poweredOn,
              let central else { return }
        expireMic()
        var budget = 64
        while budget > 0 {
            let channel: String
            let data: Data
            if let value = controls.first?.first { channel = "control"; data = value }
            else if let value = flow { channel = "flow"; data = value }
            else if let value = mic.first?.data { channel = "mic"; data = value }
            else { return }
            guard let characteristic = chars[channel] else { return }
            // For indication-only ControlTX the OS owns indication acknowledgements.
            if !manager.updateValue(data, for: characteristic, onSubscribedCentrals: [central]) {
                blocked = true
                backpressure += 1
                return // Keep this exact value until the ready callback, without a busy retry.
            }
            if channel == "control" {
                controls[0].removeFirst()
                if controls[0].isEmpty { controls.removeFirst() }
            } else if channel == "flow" { flow = nil }
            else { mic.removeFirst() }
            budget -= 1
        }
        DispatchQueue.main.async { [weak self] in self?.drain() }
    }

    func peripheralManagerIsReady(toUpdateSubscribers peripheral: CBPeripheralManager) {
        guard peripheral === manager else { return }
        blocked = false
        drain()
    }

    private func tick() {
        expireMic()
        if let assembling, monoNS() - assembling.at >= 2_000_000_000 {
            controlFailure("outgoing control assembly timeout")
        }
        _ = send(["type": "bridge_metrics", "mic_queued": mic.count,
                  "control_transfers_queued": controls.count, "flow_queued": flow != nil,
                  "mic_dropped": droppedMic, "flow_coalesced": droppedFlow,
                  "control_dropped": droppedControl, "backpressure": backpressure,
                  "writes_rejected": rejectedWrites, "socket_queued": outbound.count,
                  "central_id": central?.identifier.uuidString ?? "",
                  "max_value": min(244, central?.maximumUpdateValueLength ?? 20),
                  "subscriptions": subscriptions.map { $0.uuidString }.sorted(),
                  "security": "macOS encryption requested; passkey/MITM not observable or replicated"])
    }
}

let application = NSApplication.shared
let bridge = BLEBridge()
application.delegate = bridge
application.run()
