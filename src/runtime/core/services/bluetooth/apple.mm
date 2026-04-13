#if defined(__APPLE__)
#import <Foundation/Foundation.h>
#import <CoreBluetooth/CoreBluetooth.h>

#include "../../../platform/system.hh"
#include "../../../debug.hh"
#include "../bluetooth.hh"
#include "../../../bytes.hh"
#include "../../../app.hh"
#include "../../../http.hh"
#include "../../../string.hh"
#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

using oro::runtime::core::services::Bluetooth;
using oro::runtime::types::Map;
using oro::runtime::types::String;
using oro::runtime::types::Vector;
namespace JSON = oro::runtime::JSON;
namespace bytes = oro::runtime::bytes;

@interface OROBluetoothCentral : NSObject<CBCentralManagerDelegate, CBPeripheralDelegate>
@property (nonatomic, strong) CBCentralManager* central;
@property (nonatomic, assign) BOOL ready;
@property (nonatomic, assign) CBManagerState state;
@property (nonatomic, strong) NSMutableDictionary<NSString*, CBPeripheral*>* peripherals;
@property (nonatomic, strong) NSMutableDictionary<NSString*, void(^)(NSError*)>* onConnect;
@property (nonatomic, strong) NSMutableDictionary<NSString*, void(^)(NSError*)>* onDisconnect;
@property (nonatomic, strong) NSMutableDictionary<NSString*, void(^)(NSError*, NSArray<CBService*>*)>* onServices;
@property (nonatomic, strong) NSMutableDictionary<NSString*, NSMutableDictionary<NSString*, void(^)(NSError*, NSArray<CBCharacteristic*>*)>*>* onCharacteristics;
@property (nonatomic, strong) NSMutableDictionary<NSString*, void(^)(NSError*, NSData*)>* onRead;
@property (nonatomic, strong) NSMutableDictionary<NSString*, void(^)(NSError*)>* onWrite;
@property (nonatomic, copy) void (^onNotify)(NSString* deviceId, NSString* serviceId, NSString* charId, NSData* value);
@property (nonatomic, copy) void (^foundDevice)(CBPeripheral* peripheral, NSDictionary* adv, NSNumber* rssi);
@property (nonatomic, copy) void (^scanStopped)(void);
@property (nonatomic, strong) NSArray<CBUUID*>* filterServices;
@property (nonatomic, strong) NSString* filterName;
@property (nonatomic, strong) NSString* filterNamePrefix;
@property (nonatomic, copy) void (^onStateChange)(CBManagerState state);
@end

@implementation OROBluetoothCentral
- (id) init {
  self = [super init];
  if (self) {
    self.ready = NO;
    self.state = CBManagerStateUnknown;
    // Create central manager on main queue, we only query state here
    self.central = [[CBCentralManager alloc] initWithDelegate:self queue:dispatch_get_main_queue() options:nil];
    self.peripherals = [NSMutableDictionary new];
    self.onConnect = [NSMutableDictionary new];
    self.onDisconnect = [NSMutableDictionary new];
    self.onServices = [NSMutableDictionary new];
    self.onCharacteristics = [NSMutableDictionary new];
    self.onRead = [NSMutableDictionary new];
    self.onWrite = [NSMutableDictionary new];
    self.filterServices = nil;
    self.filterName = nil;
    self.filterNamePrefix = nil;
  }
  return self;
}

- (void) centralManagerDidUpdateState:(CBCentralManager*)central {
  self.state = central.state;
  self.ready = YES;
  if (self.onStateChange) {
    self.onStateChange(self.state);
  }
}

- (void) centralManager:(CBCentralManager *)central didDiscoverPeripheral:(CBPeripheral *)peripheral advertisementData:(NSDictionary<NSString *,id> *)advertisementData RSSI:(NSNumber *)RSSI {
  if (!peripheral || peripheral.identifier == nil) return;
  NSString *localName = advertisementData[CBAdvertisementDataLocalNameKey];
  NSArray<CBUUID*> *services = advertisementData[CBAdvertisementDataServiceUUIDsKey] ?: @[];

  BOOL matches = YES;
  if (self.filterServices.count > 0) {
    matches = NO;
    for (CBUUID *u in services) {
      for (CBUUID *f in self.filterServices) {
        if ([u isEqual:f]) { matches = YES; break; }
      }
      if (matches) break;
    }
  }
  if (matches && self.filterName) {
    matches = (localName && [localName isEqualToString:self.filterName]);
  }
  if (matches && self.filterNamePrefix) {
    matches = (localName && [localName hasPrefix:self.filterNamePrefix]);
  }

  if (!matches) return;

  NSString *key = peripheral.identifier.UUIDString;
  if (key) {
    self.peripherals[key] = peripheral;
  }
  if (self.foundDevice) {
    self.foundDevice(peripheral, advertisementData, RSSI);
  }
}

- (void) centralManager:(CBCentralManager *)central didConnectPeripheral:(CBPeripheral *)peripheral {
  NSString *key = peripheral.identifier.UUIDString;
  if (key && self.onConnect[key]) { auto cb = self.onConnect[key]; [self.onConnect removeObjectForKey:key]; cb(nil); }
}

- (void) centralManager:(CBCentralManager *)central didFailToConnectPeripheral:(CBPeripheral *)peripheral error:(NSError *)error {
  NSString *key = peripheral.identifier.UUIDString;
  if (key && self.onConnect[key]) { auto cb = self.onConnect[key]; [self.onConnect removeObjectForKey:key]; cb(error ?: [NSError errorWithDomain:@"CoreBluetooth" code:-1 userInfo:nil]); }
}

- (void) centralManager:(CBCentralManager *)central didDisconnectPeripheral:(CBPeripheral *)peripheral error:(NSError *)error {
  NSString *key = peripheral.identifier.UUIDString;
  if (key && self.onDisconnect[key]) { auto cb = self.onDisconnect[key]; [self.onDisconnect removeObjectForKey:key]; cb(error); }
  // Emit JS event for unexpected disconnects as well
  auto app = oro::runtime::app::App::sharedApplication();
  if (app) {
    oro::runtime::JSON::Object evt = oro::runtime::JSON::Object::Entries {{
      {"deviceId", oro::runtime::string::String([[peripheral.identifier UUIDString] UTF8String])},
      {"reason", error ? oro::runtime::string::String([[error localizedDescription] UTF8String]) : oro::runtime::string::String("")}
    }};
    for (const auto& win : app->runtime.windowManager.windows) {
      if (win && win->bridge) win->bridge->emit("bluetooth.gattserverdisconnected", evt.str());
    }
  }
}
@end

namespace {
  class AppleBackend final : public Bluetooth::Backend {
    OROBluetoothCentral* controller = nil;
    oro::runtime::core::Service& svc;
    // pending requestDevice state
    bool scanning = false;
    std::string pendingSeq;
    oro::runtime::core::Service::Callback pendingCb = nullptr;
    Bluetooth::ParsedRequestDeviceOptions requestFilters;
    std::unordered_set<std::string> seenPeripherals;
    mutable std::mutex stateWaitersMutex;
    mutable std::vector<std::shared_ptr<std::promise<CBManagerState>>> stateWaiters;

    void notifyStateWaiters(CBManagerState state) const {
      std::vector<std::shared_ptr<std::promise<CBManagerState>>> waiters;
      {
        std::lock_guard<std::mutex> lock(stateWaitersMutex);
        waiters.swap(stateWaiters);
      }
      for (const auto& waiter : waiters) {
        if (!waiter) continue;
        try {
          waiter->set_value(state);
        } catch (...) {
        }
      }
    }

    bool waitForManagerState(CBManagerState& outState, int timeoutMs = 500) const {
      __block CBManagerState state = CBManagerStateUnknown;
      __block BOOL ready = NO;
      dispatch_sync(dispatch_get_main_queue(), ^{
        ready = controller.ready;
        state = controller.state;
      });
      if (ready) {
        outState = state;
        return true;
      }

      auto promise = std::make_shared<std::promise<CBManagerState>>();
      auto future = promise->get_future();
      {
        std::lock_guard<std::mutex> lock(stateWaitersMutex);
        stateWaiters.push_back(promise);
      }

      __weak AppleBackend* weakSelf = const_cast<AppleBackend*>(this);
      dispatch_async(dispatch_get_main_queue(), ^{
        AppleBackend* strongSelf = weakSelf;
        if (!strongSelf) return;
        if (strongSelf->controller.ready) {
          strongSelf->notifyStateWaiters(strongSelf->controller.state);
        }
      });

      const auto status = future.wait_for(std::chrono::milliseconds(timeoutMs));
      if (status == std::future_status::ready) {
        try {
          outState = future.get();
        } catch (...) {
          dispatch_sync(dispatch_get_main_queue(), ^{
            outState = controller.state;
          });
        }
        return true;
      }

      {
        std::lock_guard<std::mutex> lock(stateWaitersMutex);
        auto it = std::remove_if(stateWaiters.begin(), stateWaiters.end(), [&](const auto& entry) {
          return entry == promise;
        });
        stateWaiters.erase(it, stateWaiters.end());
      }

      dispatch_sync(dispatch_get_main_queue(), ^{
        ready = controller.ready;
        state = controller.state;
      });
      outState = state;
      return ready;
    }
  public:
    AppleBackend(Bluetooth& service) : svc(service) {
      controller = [OROBluetoothCentral new];
      __weak Bluetooth* weakSvc = static_cast<Bluetooth*>(&service);
      __weak AppleBackend* weakBackend = this;
      controller.onNotify = ^(NSString* dev, NSString* svcId, NSString* chrId, NSData* value) {
        auto *wb = weakSvc;
        if (!wb) return;
        oro::runtime::bytes::Buffer bd(value.length);
        if (value.length > 0) memcpy(bd.data(), value.bytes, value.length);
        wb->notifyCharacteristicValue(String([dev UTF8String]), String([svcId UTF8String]), String([chrId UTF8String]), bd);
      };
      controller.onStateChange = ^(CBManagerState state) {
        AppleBackend* strongBackend = weakBackend;
        if (!strongBackend) return;
        strongBackend->notifyStateWaiters(state);
      };
    }
    ~AppleBackend() override {
      if (controller) {
        controller.onStateChange = nil;
        controller.onNotify = nil;
      }
    #if !__has_feature(objc_arc)
      [controller release];
    #endif
      controller = nil;
    }

    void getAvailability(const std::string& seq, const oro::runtime::core::Service::Callback cb) const override {
      const auto runtime = svc.context.getRuntime();
      const bool allowed = runtime ? runtime->hasPermission("bluetooth") : true;
      CBManagerState state = CBManagerStateUnknown;
      waitForManagerState(state);
      const bool hwReady = (state == CBManagerStatePoweredOn);
    JSON::Object json = JSON::Object::Entries {{
      "data", JSON::Object::Entries {{"available", allowed && hwReady}}
    }};
    cb(seq, json, oro::runtime::QueuedResponse{});
  }

    void getDevices(const std::string& seq, const oro::runtime::core::Service::Callback cb) override {
      JSON::Object json = JSON::Object::Entries {{
        "data", JSON::Object::Entries {{"devices", JSON::Array {}}}
      }};
      cb(seq, json, oro::runtime::QueuedResponse{});
    }

    void requestDevice(const std::string& seq, const JSON::Any& options, const oro::runtime::core::Service::Callback cb) override {
      if (!svc.context.getRuntime()->hasPermission("bluetooth")) {
        JSON::Object json = JSON::Object::Entries {{
          "err", JSON::Object::Entries {{
            {"type", "NotAllowedError"},
            {"message", "Bluetooth permission is disabled by runtime configuration"}
          }}
        }};
        cb(seq, json, oro::runtime::QueuedResponse{});
        return;
      }

      // Validate central state
      CBManagerState st = CBManagerStateUnknown;
      waitForManagerState(st);
      if (st != CBManagerStatePoweredOn) {
        JSON::Object json = JSON::Object::Entries {{
          "err", JSON::Object::Entries {{
            {"type", "NotSupportedError"},
            {"message", "Bluetooth is not powered on"}
          }}
        }};
        cb(seq, json, oro::runtime::QueuedResponse{});
        return;
      }

      // Parse options
      auto matchMode = Bluetooth::ParsedRequestDeviceOptions::ServiceMatchMode::All;
      if (auto runtime = svc.context.getRuntime()) {
        const auto& cfg = runtime->userConfig;
        if (cfg.contains("web_bluetooth_services_match")) {
          auto mode = oro::runtime::string::toLowerCase(cfg.at("web_bluetooth_services_match"));
          if (mode == "any") {
            matchMode = Bluetooth::ParsedRequestDeviceOptions::ServiceMatchMode::Any;
          }
        }
      }

      JSON::Object error;
      Bluetooth::ParsedRequestDeviceOptions parsed;
      if (!Bluetooth::parseRequestDeviceOptions(options, parsed, &error, matchMode)) {
        cb(seq, JSON::Object::Entries {{"err", error}}, oro::runtime::QueuedResponse{});
        return;
      }

      requestFilters = parsed;
      seenPeripherals.clear();

      NSMutableArray<CBUUID*>* uuids = [NSMutableArray new];
      std::unordered_set<std::string> seenServices;
      for (const auto& filter : requestFilters.filters) {
        for (const auto& svcId : filter.services) {
          std::string key = svcId.c_str();
          if (!seenServices.insert(key).second) continue;
          NSString* ns = [NSString stringWithUTF8String:key.c_str()];
          if (!ns) continue;
          CBUUID* uuid = [CBUUID UUIDWithString:ns];
          if (uuid) [uuids addObject:uuid];
        }
      }
      controller.filterServices = uuids.count > 0 ? uuids : nil;
      controller.filterName = nil;
      controller.filterNamePrefix = nil;

      // Start scan and emit devicefound events to all windows; wait for chooseDevice
      scanning = true;
      pendingSeq = seq;
      pendingCb = cb;

      int timeoutMs = requestFilters.timeoutMs;
      if (timeoutMs <= 0) {
        if (auto runtime = svc.context.getRuntime()) {
          const auto& cfg = runtime->userConfig;
          if (cfg.contains("web_bluetooth_timeout_ms")) {
            try {
              timeoutMs = std::stoi(cfg.at("web_bluetooth_timeout_ms"));
            } catch (...) {
              timeoutMs = -1;
            }
          }
        }
      }
      if (timeoutMs <= 0) timeoutMs = 12000; // default 12 seconds
      controller.foundDevice = ^(CBPeripheral* peripheral, NSDictionary* adv, NSNumber* rssi) {
        String deviceId = String([[peripheral.identifier UUIDString] UTF8String]);
        String deviceName = peripheral.name ? String([peripheral.name UTF8String]) : String("");
        NSString* advLocalName = adv[CBAdvertisementDataLocalNameKey];
        if (deviceName.size() == 0 && advLocalName) {
          deviceName = String([advLocalName UTF8String]);
        }

        Vector<String> services;
        NSArray<CBUUID*>* advServices = adv[CBAdvertisementDataServiceUUIDsKey];
        if (advServices) {
          for (CBUUID* uuid in advServices) {
            const char* suuid = [[uuid UUIDString] UTF8String];
            if (suuid) services.push_back(Bluetooth::normalizeUUID(String(suuid)));
          }
        }

        Map<uint16_t, Vector<uint8_t>> manufacturerData;
        NSData* manufacturer = adv[CBAdvertisementDataManufacturerDataKey];
        if (manufacturer && manufacturer.length >= 2) {
          const uint8_t* bytes = (const uint8_t*) manufacturer.bytes;
          uint16_t companyId = (uint16_t) bytes[0] | ((uint16_t) bytes[1] << 8);
          Vector<uint8_t> payload;
          if (manufacturer.length > 2) {
            payload.reserve(manufacturer.length - 2);
            for (NSUInteger i = 2; i < manufacturer.length; ++i) {
              payload.push_back(bytes[i]);
            }
          }
          manufacturerData[companyId] = payload;
        }

        if (!Bluetooth::anyFilterMatches(requestFilters, deviceName, services, manufacturerData)) {
          return;
        }

        if (!seenPeripherals.insert(std::string(deviceId.c_str())).second) {
          return;
        }

        JSON::Object evt = JSON::Object::Entries {{
          {"id", deviceId},
          {"name", deviceName},
          {"rssi", rssi ? (int64_t) rssi.integerValue : (int64_t)0}
        }};
        auto app = oro::runtime::app::App::sharedApplication();
        if (app) {
          for (const auto& win : app->runtime.windowManager.windows) {
            if (win && win->bridge) {
              win->bridge->emit("bluetooth.devicefound", evt.str());
            }
          }
        }
      };

      // Inform renderer to present chooser
      {
        auto app = oro::runtime::app::App::sharedApplication();
        if (app) {
          JSON::Object evt = JSON::Object::Entries {{"reason", "requestDevice"}};
          for (const auto& win : app->runtime.windowManager.windows) {
            if (win && win->bridge) win->bridge->emit("bluetooth.chooserequest", evt.str());
          }
        }
      }

      dispatch_async(dispatch_get_main_queue(), ^{
        [controller.central scanForPeripheralsWithServices:controller.filterServices options:@{ CBCentralManagerScanOptionAllowDuplicatesKey: @NO }];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) timeoutMs * 1000000LL), dispatch_get_main_queue(), ^{
          if (scanning) {
            scanning = false;
            [controller.central stopScan];
            JSON::Object json = JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "NotFoundError"}, {"message", "No device chosen"}}}};
            if (pendingCb) pendingCb(seq, json, oro::runtime::QueuedResponse{});
            pendingSeq.clear(); pendingCb = nullptr;
          }
        });
      });
    }

    // chooseDevice: finalize the pending request with the selected device id
    void chooseDevice(const std::string& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::core::Service::Callback cb) override {
      if (!scanning) return;
      scanning = false;
      seenPeripherals.clear();
      dispatch_async(dispatch_get_main_queue(), ^{ [controller.central stopScan]; });
      NSString* key = [NSString stringWithUTF8String:deviceId.c_str()];
      CBPeripheral* peripheral = controller.peripherals[key];
      JSON::Object json;
      if (peripheral) {
        JSON::Object device = JSON::Object::Entries {{
          {"id", String([[peripheral.identifier UUIDString] UTF8String])},
          {"name", peripheral.name ? String([peripheral.name UTF8String]) : String("")}
        }};
        json = JSON::Object::Entries {{"data", JSON::Object::Entries {{"device", device}}}};
      } else {
        json = JSON::Object::Entries {{"err", JSON::Object::Entries {{"type","NotFoundError"},{"message","Device not found"}}}};
      }
      // Resolve the chooseDevice IPC call itself
      cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
      // Also resolve the pending requestDevice if present
      if (!pendingSeq.empty() && pendingCb) {
        pendingCb(pendingSeq, json, oro::runtime::QueuedResponse{});
        pendingSeq.clear(); pendingCb = nullptr;
      }
    }

    void cancelRequest(const std::string& seq, const oro::runtime::core::Service::Callback cb) override {
      if (!scanning) { cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type","AbortError"},{"message","Cancelled"}}}}, oro::runtime::QueuedResponse{}); return; }
      scanning = false;
      seenPeripherals.clear();
      dispatch_async(dispatch_get_main_queue(), ^{ [controller.central stopScan]; });
      cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type","AbortError"},{"message","Cancelled"}}}}, oro::runtime::QueuedResponse{});
      if (!pendingSeq.empty() && pendingCb) {
        pendingCb(pendingSeq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type","AbortError"},{"message","Cancelled"}}}}, oro::runtime::QueuedResponse{});
        pendingSeq.clear(); pendingCb = nullptr;
      }
    }

    void restartDiscovery(const std::string& seq, const oro::runtime::core::Service::Callback cb) override {
      JSON::Object json = JSON::Object::Entries {{
        "err", JSON::Object::Entries {{"type","NotSupportedError"},{"message","Restarting discovery is not supported on Apple yet"}}
      }};
      cb(seq, json, oro::runtime::QueuedResponse{});
    }

    void deviceWatchAdvertisements(const std::string& seq, const Bluetooth::DeviceID&, const oro::runtime::core::Service::Callback cb) override {
      JSON::Object err = JSON::Object::Entries {{
        {"type", "NotSupportedError"},
        {"message", String("BluetoothDevice.watchAdvertisements is not supported on Apple platforms yet")}
      }};
      cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
    }

    void deviceForget(const std::string& seq, const Bluetooth::DeviceID&, const oro::runtime::core::Service::Callback cb) override {
      JSON::Object err = JSON::Object::Entries {{
        {"type", "NotSupportedError"},
        {"message", String("BluetoothDevice.forget is not supported on Apple platforms yet")}
      }};
      cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
    }

    void gattConnect(const std::string& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::core::Service::Callback cb) override {
      NSString* keyIn = [NSString stringWithUTF8String:deviceId.c_str()];
      CBPeripheral* peripheral = controller.peripherals[keyIn];
      if (!peripheral) {
        // Try retrieval by UUID (previously seen device)
        NSUUID* uuid = [[NSUUID alloc] initWithUUIDString:keyIn];
        if (uuid) {
          NSArray<CBPeripheral*>* found = [controller.central retrievePeripheralsWithIdentifiers:@[uuid]];
          if (found.count > 0) { peripheral = found.firstObject; controller.peripherals[keyIn] = peripheral; }
        }
      }
      if (!peripheral) {
        JSON::Object err = JSON::Object::Entries {{"type","NotFoundError"},{"message","Peripheral not found"}};
        cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        return;
      }

      NSString* key = peripheral.identifier.UUIDString;
      controller.onConnect[key] = ^(NSError* error){
        if (error) {
          JSON::Object err = JSON::Object::Entries {{"type","NetworkError"},{"message", String([[error localizedDescription] UTF8String])}};
          cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        } else {
          cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
        }
      };
      dispatch_async(dispatch_get_main_queue(), ^{ [controller.central connectPeripheral:peripheral options:nil]; });
    }
    void gattDisconnect(const std::string& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::core::Service::Callback cb) override {
      NSString* key = [NSString stringWithUTF8String:deviceId.c_str()];
      CBPeripheral* peripheral = controller.peripherals[key];
      if (!peripheral) { cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{}); return; }
      controller.onDisconnect[key] = ^(NSError* error){ (void)error; cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{}); };
      dispatch_async(dispatch_get_main_queue(), ^{ [controller.central cancelPeripheralConnection:peripheral]; });
    }
    void gattGetPrimaryService(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceUuid, const oro::runtime::core::Service::Callback cb) override {
      // Use getPrimaryServices then filter; return ok if present
      this->gattGetPrimaryServices(seq, deviceId, serviceUuid, [cb, serviceUuid](auto s, auto json, auto qr){
        const auto& jsonObject = json.template as<JSON::Object>();
        if (jsonObject.has("err")) return cb(s, json, qr);
        const auto& services = jsonObject.get("data").template as<JSON::Object>().get("services").template as<JSON::Array>();
        bool found = false;
        for (size_t i = 0; i < services.size(); ++i) { if (services[i].str() == serviceUuid) { found = true; break; } }
        if (!found) {
          JSON::Object err = JSON::Object::Entries {{"type","NotFoundError"},{"message","Service not found"}};
          cb(s, JSON::Object::Entries {{"err", err}}, qr);
        } else {
          cb(s, JSON::Object::Entries {{"data", JSON::Object::Entries {{"service", serviceUuid}}}}, qr);
        }
      });
    }
    void gattGetPrimaryServices(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceFilterUuid, const oro::runtime::core::Service::Callback cb) override {
      NSString* devKey = [NSString stringWithUTF8String:deviceId.c_str()];
      CBPeripheral* peripheral = controller.peripherals[devKey];
      if (!peripheral) {
        JSON::Object err = JSON::Object::Entries {{"type","NotFoundError"},{"message","Peripheral not connected"}};
        cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        return;
      }
      peripheral.delegate = controller;
      NSString* dev = peripheral.identifier.UUIDString;

      const bool hasFilter = !serviceFilterUuid.empty();
      String normalizedFilter;
      if (hasFilter) {
        normalizedFilter = Bluetooth::normalizeUUID(String(serviceFilterUuid.c_str()));
      }

      auto respondWithServices = ^(NSArray<CBService*>* services) {
        JSON::Array arr;
        for (CBService* svc in services) {
          String uuid = String([[svc.UUID UUIDString] UTF8String]);
          String normalized = Bluetooth::normalizeUUID(uuid);
          if (!normalizedFilter.empty() && normalized != normalizedFilter) {
            continue;
          }
          arr.push(normalized);
        }
        JSON::Object json = JSON::Object::Entries {{"data", JSON::Object::Entries {{"services", arr}}}};
        cb(seq, json, oro::runtime::QueuedResponse{});
      };

      auto hasMatch = ^bool (NSArray<CBService*>* services) {
        if (!hasFilter) return services.count > 0;
        for (CBService* svc in services) {
          String uuid = String([[svc.UUID UUIDString] UTF8String]);
          if (Bluetooth::normalizeUUID(uuid) == normalizedFilter) {
            return true;
          }
        }
        return false;
      };

      NSArray<CBService*>* cached = peripheral.services;
      if (cached.count > 0 && (!hasFilter || hasMatch(cached))) {
        respondWithServices(cached);
        return;
      }

      controller.onServices[dev] = ^(NSError* error, NSArray<CBService*>* services){
        if (error) {
          JSON::Object err = JSON::Object::Entries {{"type","NetworkError"},{"message", String([[error localizedDescription] UTF8String])}};
          cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        } else {
          respondWithServices(services);
        }
      };

      NSArray<CBUUID*>* serviceFilter = nil;
      if (hasFilter) {
        NSString* s = [NSString stringWithUTF8String:serviceFilterUuid.c_str()];
        if (s.length > 0) {
          CBUUID* uuid = [CBUUID UUIDWithString:s];
          if (uuid) {
            serviceFilter = @[uuid];
          }
        }
      }

      dispatch_async(dispatch_get_main_queue(), ^{ [peripheral discoverServices:serviceFilter]; });
    }
    void serviceGetCharacteristic(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceUuid, const std::string& charUuid, const oro::runtime::core::Service::Callback cb) override {
      this->serviceGetCharacteristics(seq, deviceId, serviceUuid, "", [cb, charUuid](auto s, auto json, auto qr){
        const auto& jsonObject = json.template as<JSON::Object>();
        if (jsonObject.has("err")) return cb(s, json, qr);
        const auto& chars = jsonObject.get("data").template as<JSON::Object>().get("characteristics").template as<JSON::Array>();
        bool found = false; for (size_t i = 0; i < chars.size(); ++i) { if (chars[i].str() == charUuid) { found = true; break; } }
        if (!found) {
          JSON::Object err = JSON::Object::Entries {{"type","NotFoundError"},{"message","Characteristic not found"}};
          cb(s, JSON::Object::Entries {{"err", err}}, qr);
        } else {
          cb(s, JSON::Object::Entries {{"data", JSON::Object::Entries {{"characteristic", charUuid}}}}, qr);
        }
      });
    }
    void serviceGetCharacteristics(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceUuid, const std::string&, const oro::runtime::core::Service::Callback cb) override {
      NSString* devKey = [NSString stringWithUTF8String:deviceId.c_str()];
      CBPeripheral* peripheral = controller.peripherals[devKey];
      if (!peripheral) {
        JSON::Object err = JSON::Object::Entries {{"type","NotFoundError"},{"message","Peripheral not connected"}};
        cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        return;
      }
      peripheral.delegate = controller;
      CBUUID* svcId = [CBUUID UUIDWithString:[NSString stringWithUTF8String:serviceUuid.c_str()]];
      // Helper block to discover characteristics and respond
      void (^discoverChars)(CBPeripheral*, CBService*) = ^(CBPeripheral* p, CBService* svc){
        NSString* dev2 = p.identifier.UUIDString;
        if (!controller.onCharacteristics[dev2]) controller.onCharacteristics[dev2] = [NSMutableDictionary new];
        controller.onCharacteristics[dev2][svc.UUID.UUIDString] = ^(NSError* error, NSArray<CBCharacteristic*>* list){
          if (error) {
            JSON::Object err = JSON::Object::Entries {{"type","NetworkError"},{"message", String([[error localizedDescription] UTF8String])}};
            cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
          } else {
            JSON::Array arr;
            JSON::Object propsMap;
            for (CBCharacteristic* c in list) {
              String uuid = String([[c.UUID UUIDString] UTF8String]);
              arr.push(uuid);
              auto flags = (int) c.properties;
              const bool hasExtendedProperties = (flags & CBCharacteristicPropertyExtendedProperties) != 0;
              JSON::Object props = JSON::Object::Entries {{
                {"broadcast", (bool) (flags & CBCharacteristicPropertyBroadcast)},
                {"read", (bool) (flags & CBCharacteristicPropertyRead)},
                {"writeWithoutResponse", (bool) (flags & CBCharacteristicPropertyWriteWithoutResponse)},
                {"write", (bool) (flags & CBCharacteristicPropertyWrite)},
                {"notify", (bool) (flags & CBCharacteristicPropertyNotify)},
                {"indicate", (bool) (flags & CBCharacteristicPropertyIndicate)},
                {"authenticatedSignedWrites", (bool) (flags & CBCharacteristicPropertyAuthenticatedSignedWrites)},
                {"reliableWrite", hasExtendedProperties},
                {"writableAuxiliaries", hasExtendedProperties}
              }};
              propsMap.set(uuid, props);
            }
            cb(seq, JSON::Object::Entries {{"data", JSON::Object::Entries {{"characteristics", arr}, {"propertiesByCharacteristic", propsMap}}}}, oro::runtime::QueuedResponse{});
          }
        };
        dispatch_async(dispatch_get_main_queue(), ^{ [p discoverCharacteristics:nil forService:svc]; });
      };

      CBService* target = nil;
      for (CBService* s in peripheral.services) { if ([s.UUID isEqual:svcId]) { target = s; break; } }
      if (!target) {
        // Discover services first
        NSString* dev = peripheral.identifier.UUIDString;
        controller.onServices[dev] = ^(NSError* error, NSArray<CBService*>* services){
          if (error) {
            JSON::Object err = JSON::Object::Entries {{"type","NetworkError"},{"message", String([[error localizedDescription] UTF8String])}};
            cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
          } else {
            CBService* svc = nil; for (CBService* s in services) { if ([s.UUID isEqual:svcId]) { svc = s; break; } }
            if (!svc) {
              JSON::Object err = JSON::Object::Entries {{"type","NotFoundError"},{"message","Service not found"}};
              cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
            } else {
              discoverChars(peripheral, svc);
            }
          }
        };
        dispatch_async(dispatch_get_main_queue(), ^{ [peripheral discoverServices:@[svcId]]; });
        return;
      }
      discoverChars(peripheral, target);
    }
    void characteristicReadValue(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceUuid, const std::string& charUuid, const oro::runtime::core::Service::Callback cb) override {
      NSString* devKey = [NSString stringWithUTF8String:deviceId.c_str()];
      CBPeripheral* peripheral = controller.peripherals[devKey];
      if (!peripheral) {
        JSON::Object err = JSON::Object::Entries {{"type","NotFoundError"},{"message","Peripheral not connected"}};
        cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        return;
      }
      peripheral.delegate = controller;
      CBUUID* svcId = [CBUUID UUIDWithString:[NSString stringWithUTF8String:serviceUuid.c_str()]];
      CBUUID* chId = [CBUUID UUIDWithString:[NSString stringWithUTF8String:charUuid.c_str()]];
      CBService* svc = nil; for (CBService* s in peripheral.services) { if ([s.UUID isEqual:svcId]) { svc = s; break; } }
      if (!svc) {
        JSON::Object err = JSON::Object::Entries {{"type","NotFoundError"},{"message","Service not discovered"}};
        cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        return;
      }
      CBCharacteristic* chr = nil; for (CBCharacteristic* c in svc.characteristics) { if ([c.UUID isEqual:chId]) { chr = c; break; } }
      if (!chr) {
        JSON::Object err = JSON::Object::Entries {{"type","NotFoundError"},{"message","Characteristic not discovered"}};
        cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        return;
      }
      NSString* chrKey = chr.UUID.UUIDString;
      controller.onRead[chrKey] = ^(NSError* error, NSData* data){
        if (error) {
          JSON::Object err = JSON::Object::Entries {{"type","OperationError"},{"message", String([[error localizedDescription] UTF8String])}};
          cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        } else {
          size_t n = data.length;
          bytes::Buffer bd(n);
          if (n > 0) memcpy(bd.data(), data.bytes, n);
          oro::runtime::http::Headers hdr; hdr.set("content-type", "application/octet-stream"); hdr.set("content-length", (uint64_t)n);
          cb(seq, JSON::Object {}, oro::runtime::QueuedResponse{ oro::runtime::crypto::rand64(), 0, bd.shared(), bd.size(), hdr.str() });
        }
      };
      dispatch_async(dispatch_get_main_queue(), ^{ [peripheral readValueForCharacteristic:chr]; });
    }
    void characteristicWriteValue(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceUuid, const std::string& charUuid, const bytes::Buffer& value, const oro::runtime::core::Service::Callback cb) override {
      NSString* devKey = [NSString stringWithUTF8String:deviceId.c_str()];
      CBPeripheral* peripheral = controller.peripherals[devKey];
      if (!peripheral) {
        JSON::Object err = JSON::Object::Entries {{"type","NotFoundError"},{"message","Peripheral not connected"}};
        cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        return;
      }
      peripheral.delegate = controller;
      CBUUID* svcId = [CBUUID UUIDWithString:[NSString stringWithUTF8String:serviceUuid.c_str()]];
      CBUUID* chId = [CBUUID UUIDWithString:[NSString stringWithUTF8String:charUuid.c_str()]];
      CBService* svc = nil; for (CBService* s in peripheral.services) { if ([s.UUID isEqual:svcId]) { svc = s; break; } }
      if (!svc) {
        JSON::Object err = JSON::Object::Entries {{"type","NotFoundError"},{"message","Service not discovered"}};
        cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        return;
      }
      CBCharacteristic* chr = nil; for (CBCharacteristic* c in svc.characteristics) { if ([c.UUID isEqual:chId]) { chr = c; break; } }
      if (!chr) {
        JSON::Object err = JSON::Object::Entries {{"type","NotFoundError"},{"message","Characteristic not discovered"}};
        cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        return;
      }
      NSData* data = [NSData dataWithBytes:value.data() length:value.size()];
      NSString* chrKey = chr.UUID.UUIDString;
      controller.onWrite[chrKey] = ^(NSError* error){
        if (error) {
          JSON::Object err = JSON::Object::Entries {{"type","OperationError"},{"message", String([[error localizedDescription] UTF8String])}};
          cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        } else {
          cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
        }
      };
      dispatch_async(dispatch_get_main_queue(), ^{ [peripheral writeValue:data forCharacteristic:chr type:CBCharacteristicWriteWithResponse]; });
    }
    void characteristicStartNotifications(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceUuid, const std::string& charUuid, const oro::runtime::core::Service::Callback cb) override {
      NSString* devKey = [NSString stringWithUTF8String:deviceId.c_str()];
      CBPeripheral* peripheral = controller.peripherals[devKey];
      if (!peripheral) {
        JSON::Object err = JSON::Object::Entries {{"type","NotFoundError"},{"message","Peripheral not connected"}};
        cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        return;
      }
      peripheral.delegate = controller;
      CBUUID* svcId = [CBUUID UUIDWithString:[NSString stringWithUTF8String:serviceUuid.c_str()]];
      CBUUID* chId = [CBUUID UUIDWithString:[NSString stringWithUTF8String:charUuid.c_str()]];
      CBService* svc = nil; for (CBService* s in peripheral.services) { if ([s.UUID isEqual:svcId]) { svc = s; break; } }
      if (!svc) {
        JSON::Object err = JSON::Object::Entries {{"type","NotFoundError"},{"message","Service not discovered"}};
        cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        return;
      }
      CBCharacteristic* chr = nil; for (CBCharacteristic* c in svc.characteristics) { if ([c.UUID isEqual:chId]) { chr = c; break; } }
      if (!chr) {
        JSON::Object err = JSON::Object::Entries {{"type","NotFoundError"},{"message","Characteristic not discovered"}};
        cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        return;
      }
      dispatch_async(dispatch_get_main_queue(), ^{ [peripheral setNotifyValue:YES forCharacteristic:chr]; });
      cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
    }
    void characteristicStopNotifications(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceUuid, const std::string& charUuid, const oro::runtime::core::Service::Callback cb) override {
      NSString* devKey = [NSString stringWithUTF8String:deviceId.c_str()];
      CBPeripheral* peripheral = controller.peripherals[devKey];
      if (!peripheral) { cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{}); return; }
      peripheral.delegate = controller;
      CBUUID* svcId = [CBUUID UUIDWithString:[NSString stringWithUTF8String:serviceUuid.c_str()]];
      CBUUID* chId = [CBUUID UUIDWithString:[NSString stringWithUTF8String:charUuid.c_str()]];
      CBService* svc = nil; for (CBService* s in peripheral.services) { if ([s.UUID isEqual:svcId]) { svc = s; break; } }
      if (svc) {
        CBCharacteristic* chr = nil; for (CBCharacteristic* c in svc.characteristics) { if ([c.UUID isEqual:chId]) { chr = c; break; } }
        if (chr) dispatch_async(dispatch_get_main_queue(), ^{ [peripheral setNotifyValue:NO forCharacteristic:chr]; });
      }
      cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
    }
  };
}

namespace oro::runtime::core::services {
  std::unique_ptr<Bluetooth::Backend> makeBluetoothBackend(Bluetooth& svc) {
    return std::unique_ptr<Bluetooth::Backend>(new AppleBackend(svc));
  }
}

@implementation OROBluetoothCentral (PeripheralCallbacks)

- (void) peripheral:(CBPeripheral *)peripheral didDiscoverServices:(NSError *)error {
  NSString* dev = peripheral.identifier.UUIDString;
  void (^cb)(NSError*, NSArray<CBService*>*) = self.onServices[dev];
  if (cb) { [self.onServices removeObjectForKey:dev]; cb(error, peripheral.services); }
}

- (void) peripheral:(CBPeripheral *)peripheral didDiscoverCharacteristicsForService:(CBService *)service error:(NSError *)error {
  NSString* dev = peripheral.identifier.UUIDString;
  NSString* svc = service.UUID.UUIDString;
  NSMutableDictionary* m = self.onCharacteristics[dev];
  void (^cb)(NSError*, NSArray<CBCharacteristic*>*) = m[svc];
  if (cb) { [m removeObjectForKey:svc]; cb(error, service.characteristics); }
}

- (void) peripheral:(CBPeripheral *)peripheral didUpdateValueForCharacteristic:(CBCharacteristic *)characteristic error:(NSError *)error {
  NSString* dev = peripheral.identifier.UUIDString;
  NSString* chr = characteristic.UUID.UUIDString;
  void (^cb)(NSError*, NSData*) = self.onRead[chr];
  if (cb) {
    [self.onRead removeObjectForKey:chr];
    cb(error, characteristic.value);
  }
  if (!error && self.onNotify) {
    NSString* svc = characteristic.service.UUID.UUIDString;
    self.onNotify(dev, svc, chr, characteristic.value ?: [NSData data]);
  }
}

- (void) peripheral:(CBPeripheral *)peripheral didWriteValueForCharacteristic:(CBCharacteristic *)characteristic error:(NSError *)error {
  NSString* chr = characteristic.UUID.UUIDString;
  void (^cb)(NSError*) = self.onWrite[chr];
  if (cb) { [self.onWrite removeObjectForKey:chr]; cb(error); }
}

@end

#endif // __APPLE__
