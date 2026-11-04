import CoreBluetooth
import Foundation
import WatchKit

/// A Mac running Infinite that this watch has seen.
struct NearbyMac: Identifiable, Equatable {
    let id: UUID
    var name: String
}

/// Bluetooth link to Infinite on the Mac. The Mac is the peripheral (it advertises the "Infinite Remote" service);
/// the watch scans, connects, subscribes to `control`, writes its name once and then streams 44-byte packets to
/// `sample` without response. UUIDs are the ones in src/platform/WatchLink.h.
final class BLELink: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    enum State: Equatable { case off, unauthorized, scanning, connecting(String), connected(String) }

    static let service = CBUUID(string: "9A1F0001-6B6D-4F1B-9E3C-1AF1D17E0001")
    static let sampleId = CBUUID(string: "9A1F0002-6B6D-4F1B-9E3C-1AF1D17E0001")
    static let controlId = CBUUID(string: "9A1F0003-6B6D-4F1B-9E3C-1AF1D17E0001")
    static let nameId = CBUUID(string: "9A1F0004-6B6D-4F1B-9E3C-1AF1D17E0001")
    private static let lastKey = "lastMac"

    @Published private(set) var macs: [NearbyMac] = []
    @Published private(set) var state: State = .off
    @Published private(set) var lastMacName: String?

    private var central: CBCentralManager!
    private var found: [UUID: CBPeripheral] = [:]
    private var target: CBPeripheral?
    private var sample: CBCharacteristic?
    private var wantConnected = false
    private let lock = NSLock()   // protects `link` for send(), which runs on the motion queue
    private var link: (CBPeripheral, CBCharacteristic)?

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    var isConnected: Bool { if case .connected = state { return true } else { return false } }

    // MARK: choosing a Mac

    func connect(_ mac: NearbyMac) {
        guard let p = found[mac.id] else { return }
        UserDefaults.standard.set(mac.id.uuidString, forKey: BLELink.lastKey)
        UserDefaults.standard.set(mac.name, forKey: BLELink.lastKey + "Name")
        lastMacName = mac.name
        open(p, name: mac.name)
    }

    func disconnect() {
        wantConnected = false
        if let p = target { central.cancelPeripheralConnection(p) }
        target = nil
        setLink(nil)
        startScan()
    }

    private func open(_ p: CBPeripheral, name: String) {
        wantConnected = true
        target = p
        p.delegate = self
        state = .connecting(name)
        central.stopScan()
        central.connect(p)
    }

    private func startScan() {
        guard central.state == .poweredOn, !wantConnected else { return }
        state = .scanning
        central.scanForPeripherals(withServices: [BLELink.service], options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
    }

    /// The Mac used last time, reconnected without asking.
    private func reconnectLast() {
        let d = UserDefaults.standard
        lastMacName = d.string(forKey: BLELink.lastKey + "Name")
        guard let s = d.string(forKey: BLELink.lastKey), let id = UUID(uuidString: s),
              let p = central.retrievePeripherals(withIdentifiers: [id]).first else { return }
        found[id] = p
        open(p, name: lastMacName ?? "Mac")
    }

    // MARK: sending

    private func setLink(_ l: (CBPeripheral, CBCharacteristic)?) {
        lock.lock(); link = l; lock.unlock()
    }

    /// False when the radio is not ready for another packet; the caller keeps its edges and tries on the next tick.
    func send(_ data: Data) -> Bool {
        lock.lock(); let l = link; lock.unlock()
        guard let (p, c) = l, p.canSendWriteWithoutResponse else { return false }
        p.writeValue(data, for: c, type: .withoutResponse)
        return true
    }

    // MARK: CBCentralManagerDelegate

    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        switch c.state {
        case .poweredOn:
            reconnectLast()
            startScan()
        case .unauthorized: state = .unauthorized
        default: state = .off
        }
    }

    func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral, advertisementData: [String: Any], rssi: NSNumber) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? p.name ?? "Mac"
        found[p.identifier] = p
        let mac = NearbyMac(id: p.identifier, name: name.replacingOccurrences(of: "Infinite on ", with: ""))
        if let i = macs.firstIndex(where: { $0.id == mac.id }) { macs[i] = mac } else { macs.append(mac) }
    }

    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        p.discoverServices([BLELink.service])
    }

    func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        if wantConnected { c.connect(p) }
    }

    func centralManager(_ c: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        setLink(nil)
        if wantConnected {
            state = .connecting(lastMacName ?? "Mac")
            c.connect(p)       // pending until the Mac is back in range
        } else {
            startScan()
        }
    }

    // MARK: CBPeripheralDelegate

    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard let s = p.services?.first(where: { $0.uuid == BLELink.service }) else { return }
        p.discoverCharacteristics([BLELink.sampleId, BLELink.controlId, BLELink.nameId], for: s)
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        var sampleC: CBCharacteristic?
        for c in s.characteristics ?? [] {
            switch c.uuid {
            case BLELink.sampleId: sampleC = c
            case BLELink.controlId: p.setNotifyValue(true, for: c)   // the Mac treats an unsubscribed watch as gone
            case BLELink.nameId:
                p.writeValue(Data(WKInterfaceDevice.current().name.utf8), for: c, type: .withResponse)
            default: break
            }
        }
        if let c = sampleC {
            setLink((p, c))
            state = .connected(lastMacName ?? p.name ?? "Mac")
        }
    }
}
