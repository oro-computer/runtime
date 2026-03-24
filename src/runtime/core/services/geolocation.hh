#ifndef ORO_RUNTIME_CORE_SERVICES_GEOLOCATION_H
#define ORO_RUNTIME_CORE_SERVICES_GEOLOCATION_H

#include "../../core.hh"

namespace oro::runtime::core::services {
  class Geolocation;
}

#if ORO_RUNTIME_PLATFORM_APPLE
@class OROLocationObserver;

@interface OROLocationManagerDelegate : NSObject<CLLocationManagerDelegate>
@property (nonatomic, strong) OROLocationObserver* locationObserver;

- (id) initWithLocationObserver: (OROLocationObserver*) locationObserver;

- (void) locationManager: (CLLocationManager*) locationManager
        didFailWithError: (NSError*) error;

- (void) locationManager: (CLLocationManager*) locationManager
      didUpdateLocations: (NSArray<CLLocation*>*) locations;

- (void)            locationManager: (CLLocationManager*) locationManager
  didFinishDeferredUpdatesWithError: (NSError*) error;

- (void) locationManagerDidPauseLocationUpdates: (CLLocationManager*) locationManager;
- (void) locationManagerDidResumeLocationUpdates: (CLLocationManager*) locationManager;
- (void) locationManager: (CLLocationManager*) locationManager
                didVisit: (CLVisit*) visit;

-       (void) locationManager: (CLLocationManager*) locationManager
  didChangeAuthorizationStatus: (CLAuthorizationStatus) status;
- (void) locationManagerDidChangeAuthorization: (CLLocationManager*) locationManager;
@end

@interface OROLocationPositionWatcher : NSObject
@property (nonatomic, assign) NSInteger identifier;
@property (nonatomic, assign) void(^completion)(CLLocation*);
+ (OROLocationPositionWatcher*) positionWatcherWithIdentifier: (NSInteger) identifier
                                                   completion: (void (^)(CLLocation*)) completion;
@end

@interface OROLocationObserver : NSObject
@property (nonatomic, retain) CLLocationManager* locationManager;
@property (nonatomic, retain) OROLocationManagerDelegate* delegate;
@property (atomic, retain) NSMutableArray* activationCompletions;
@property (atomic, retain) NSMutableArray* locationRequestCompletions;
@property (atomic, retain) NSMutableArray* locationWatchers;
@property (nonatomic) oro::runtime::core::services::Geolocation* geolocation;
@property (atomic, assign) BOOL isAuthorized;
- (BOOL) attemptActivation;
- (BOOL) attemptActivationWithCompletion: (void (^)(BOOL)) completion;
- (BOOL) getCurrentPositionWithCompletion: (void (^)(NSError*, CLLocation*)) completion;
- (int) watchPositionForIdentifier: (NSInteger) identifier
                        completion: (void (^)(NSError*, CLLocation*)) completion;
- (BOOL) clearWatch: (NSInteger) identifier;
@end
#endif

namespace oro::runtime::core::services {
  class Geolocation : public core::Service {
    public:
      using WatchID = uint64_t;
      using PermissionChangeObserver = Observer<JSON::Object>;
      using PermissionChangeObservers = Observers<PermissionChangeObserver>;

    #if ORO_RUNTIME_PLATFORM_APPLE
      OROLocationObserver* locationObserver = nullptr;
      OROLocationPositionWatcher* locationPositionWatcher = nullptr;
      OROLocationManagerDelegate* locationManagerDelegate = nullptr;
    #endif

      PermissionChangeObservers permissionChangeObservers;

      Geolocation (const Options&);
      ~Geolocation ();

      void getCurrentPosition (
        const String& seq,
        const Callback callback
      ) const;

      void watchPosition (
        const String& seq,
        const WatchID id,
        const Callback callback
      );

      void clearWatch (
        const String& seq,
        const WatchID id,
        const Callback callback
      );

      bool removePermissionChangeObserver (
        const PermissionChangeObserver& observer
      );

      bool addPermissionChangeObserver (
        const PermissionChangeObserver& observer,
        const PermissionChangeObserver::Callback callback
      );
  };
}
#endif
