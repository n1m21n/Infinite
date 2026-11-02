import Foundation
import WatchKit
#if INFINITE_HEALTH_1
import HealthKit
#endif

/// Keeps the app and its sensors running with the wrist down. Two builds, chosen in Config/Local.xcconfig:
///  - INFINITE_WATCH_HEALTH = 0 (default): a WKExtendedRuntimeSession of the physical-therapy type. Needs no
///    entitlement, so it works on a free personal team. Heart rate is not available (the heart byte stays 0).
///  - INFINITE_WATCH_HEALTH = 1: a workout session, which also gives heart rate. Needs the HealthKit capability.
protocol Keeper: AnyObject {
    /// bpm, 0 = unknown
    var heart: UInt8 { get }
    var onEnded: (() -> Void)? { get set }
    func start()
    func stop()
}

#if INFINITE_HEALTH_1

final class WorkoutKeeper: NSObject, Keeper, HKWorkoutSessionDelegate, HKLiveWorkoutBuilderDelegate {
    private let store = HKHealthStore()
    private var session: HKWorkoutSession?
    private var builder: HKLiveWorkoutBuilder?
    private let lock = NSLock()
    private var bpm: UInt8 = 0
    var onEnded: (() -> Void)?

    var heart: UInt8 { lock.lock(); defer { lock.unlock() }; return bpm }

    func start() {
        guard HKHealthStore.isHealthDataAvailable() else { return }
        let heartType = HKQuantityType(.heartRate)
        store.requestAuthorization(toShare: [HKObjectType.workoutType()], read: [heartType]) { [weak self] ok, _ in
            guard ok, let self else { return }
            DispatchQueue.main.async { self.begin() }
        }
    }

    private func begin() {
        let config = HKWorkoutConfiguration()
        config.activityType = .other
        config.locationType = .indoor
        guard let s = try? HKWorkoutSession(healthStore: store, configuration: config) else { return }
        let b = s.associatedWorkoutBuilder()
        b.dataSource = HKLiveWorkoutDataSource(healthStore: store, workoutConfiguration: config)
        s.delegate = self
        b.delegate = self
        session = s
        builder = b
        s.startActivity(with: Date())
        b.beginCollection(withStart: Date()) { _, _ in }
    }

    func stop() {
        session?.end()
        builder?.endCollection(withEnd: Date()) { [weak self] _, _ in
            self?.builder?.discardWorkout()   // a performance is not a workout to save
        }
        session = nil
        lock.lock(); bpm = 0; lock.unlock()
    }

    func workoutSession(_ s: HKWorkoutSession, didChangeTo to: HKWorkoutSessionState, from: HKWorkoutSessionState, date: Date) {
        if to == .ended { onEnded?() }
    }
    func workoutSession(_ s: HKWorkoutSession, didFailWithError error: Error) { onEnded?() }
    func workoutBuilderDidCollectEvent(_ b: HKLiveWorkoutBuilder) {}
    func workoutBuilder(_ b: HKLiveWorkoutBuilder, didCollectDataOf types: Set<HKSampleType>) {
        let type = HKQuantityType(.heartRate)
        guard types.contains(type),
              let q = b.statistics(for: type)?.mostRecentQuantity() else { return }
        let v = q.doubleValue(for: HKUnit.count().unitDivided(by: .minute()))
        lock.lock(); bpm = UInt8(max(0, min(255, v.rounded()))); lock.unlock()
    }
}

func makeKeeper() -> Keeper { WorkoutKeeper() }

#else

final class ExtendedKeeper: NSObject, Keeper, WKExtendedRuntimeSessionDelegate {
    private var session: WKExtendedRuntimeSession?
    var onEnded: (() -> Void)?
    var heart: UInt8 { 0 }

    func start() {
        let s = WKExtendedRuntimeSession()
        s.delegate = self
        s.start()
        session = s
    }

    func stop() {
        session?.invalidate()
        session = nil
    }

    func extendedRuntimeSessionDidStart(_ s: WKExtendedRuntimeSession) {}
    func extendedRuntimeSessionWillExpire(_ s: WKExtendedRuntimeSession) {}
    func extendedRuntimeSession(_ s: WKExtendedRuntimeSession, didInvalidateWith reason: WKExtendedRuntimeSessionInvalidationReason, error: Error?) {
        if session === s { session = nil; onEnded?() }
    }
}

func makeKeeper() -> Keeper { ExtendedKeeper() }

#endif
