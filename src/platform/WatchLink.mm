#include "platform/WatchLink.h"
#include "platform/WatchLinkInternal.h"

#import <CoreBluetooth/CoreBluetooth.h>
#import <Foundation/Foundation.h>

// macOS radio of the Apple Watch link: a CBPeripheralManager that advertises the "Infinite Remote" service and
// hands every write to the table in WatchLink.cpp. The delegate runs on its own serial queue, so nothing here
// touches the main thread.
@interface InfWatchPeripheral : NSObject <CBPeripheralManagerDelegate>
@property(nonatomic, strong) CBPeripheralManager* manager;
@property(nonatomic, strong) dispatch_queue_t queue;
@property(nonatomic, strong) CBMutableService* service;
@property(nonatomic, strong) CBUUID *sampleId, *controlId, *nameId, *serviceId;
- (instancetype)init;
- (void)shutdown;
@end

@implementation InfWatchPeripheral

- (instancetype)init
{
   if ((self = [super init]))
   {
      _serviceId = [CBUUID UUIDWithString:@(WatchLink::kServiceUUID)];
      _sampleId = [CBUUID UUIDWithString:@(WatchLink::kSampleUUID)];
      _controlId = [CBUUID UUIDWithString:@(WatchLink::kControlUUID)];
      _nameId = [CBUUID UUIDWithString:@(WatchLink::kNameUUID)];
      _queue = dispatch_queue_create("org.infinitedevices.watchlink", DISPATCH_QUEUE_SERIAL);
      WatchLink::Table::SetRadio(WatchLink::Transport::Radio::kStarting);
      _manager = [[CBPeripheralManager alloc] initWithDelegate:self queue:_queue];
   }
   return self;
}

- (void)shutdown
{
   // On the delegate queue, so no callback runs after the manager is gone.
   dispatch_sync(_queue, ^{
      [self.manager stopAdvertising];
      [self.manager removeAllServices];
      self.manager.delegate = nil;
      self.manager = nil;
   });
}

- (void)peripheralManagerDidUpdateState:(CBPeripheralManager*)peripheral
{
   using WatchLink::Transport::Radio;
   switch (peripheral.state)
   {
      case CBManagerStatePoweredOn:
         WatchLink::Table::SetRadio(Radio::kReady);
         [self publish];
         break;
      case CBManagerStatePoweredOff:
         WatchLink::Table::SetRadio(Radio::kPoweredOff);
         break;
      case CBManagerStateUnauthorized:
         WatchLink::Table::SetRadio(Radio::kUnauthorized);
         break;
      case CBManagerStateUnsupported:
         WatchLink::Table::SetRadio(Radio::kUnsupported);
         break;
      default:
         WatchLink::Table::SetRadio(Radio::kStarting);
         break;
   }
}

- (void)publish
{
   if (self.service != nil)
      return;
   CBMutableCharacteristic* sample =
      [[CBMutableCharacteristic alloc] initWithType:self.sampleId
                                         properties:CBCharacteristicPropertyWriteWithoutResponse | CBCharacteristicPropertyWrite
                                              value:nil
                                        permissions:CBAttributePermissionsWriteable];
   CBMutableCharacteristic* control =
      [[CBMutableCharacteristic alloc] initWithType:self.controlId
                                         properties:CBCharacteristicPropertyNotify
                                              value:nil
                                        permissions:CBAttributePermissionsReadable];
   CBMutableCharacteristic* name =
      [[CBMutableCharacteristic alloc] initWithType:self.nameId
                                         properties:CBCharacteristicPropertyWrite
                                              value:nil
                                        permissions:CBAttributePermissionsWriteable];
   self.service = [[CBMutableService alloc] initWithType:self.serviceId primary:YES];
   self.service.characteristics = @[ sample, control, name ];
   [self.manager addService:self.service];
}

- (void)peripheralManager:(CBPeripheralManager*)peripheral didAddService:(CBService*)service error:(NSError*)error
{
   if (error != nil)
      return;
   NSString* mac = NSHost.currentHost.localizedName ?: @"Mac";
   [peripheral startAdvertising:@{
      CBAdvertisementDataServiceUUIDsKey : @[ self.serviceId ],
      CBAdvertisementDataLocalNameKey : [@"Infinite on " stringByAppendingString:mac]
   }];
}

- (void)peripheralManager:(CBPeripheralManager*)peripheral didReceiveWriteRequests:(NSArray<CBATTRequest*>*)requests
{
   for (CBATTRequest* r in requests)
   {
      const std::string key = r.central.identifier.UUIDString.UTF8String;
      NSData* v = r.value;
      if ([r.characteristic.UUID isEqual:self.sampleId])
         WatchLink::Table::Deliver(key, (const uint8_t*)v.bytes, v.length);
      else if ([r.characteristic.UUID isEqual:self.nameId] && v.length > 0)
      {
         NSString* s = [[NSString alloc] initWithData:v encoding:NSUTF8StringEncoding];
         if (s != nil)
            WatchLink::Table::SetName(key, s.UTF8String);
      }
   }
   if (requests.count > 0)
      [peripheral respondToRequest:requests.firstObject withResult:CBATTErrorSuccess];
}

// The watch subscribes to `control` when it connects, which is how the Mac learns a watch left (a write-only
// characteristic gives no disconnect callback).
- (void)peripheralManager:(CBPeripheralManager*)peripheral
                  central:(CBCentral*)central
didUnsubscribeFromCharacteristic:(CBCharacteristic*)characteristic
{
   WatchLink::Table::Drop(central.identifier.UUIDString.UTF8String);
}

@end

namespace WatchLink
{
   namespace
   {
      InfWatchPeripheral* gPeripheral = nil;
   }

   namespace Transport
   {
      bool Supported() { return true; }

      void Begin()
      {
         if (gPeripheral == nil)
            gPeripheral = [[InfWatchPeripheral alloc] init];
      }

      void End()
      {
         [gPeripheral shutdown];
         gPeripheral = nil;
      }
   }
}
