// fork: macOS half of AgentNet.h. Compiled with ARC.
#include "AgentNet.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

@interface AgentNetDelegate : NSObject <NSURLSessionDataDelegate>
@property (nonatomic, copy) BOOL (^onData)(const char*, size_t);
@property (nonatomic) int status;
@property (nonatomic, copy) NSString* failure;
@property (nonatomic, strong) dispatch_semaphore_t done;
@end

@implementation AgentNetDelegate
- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)task didReceiveResponse:(NSURLResponse*)response
 completionHandler:(void (^)(NSURLSessionResponseDisposition))handler
{
   if ([response isKindOfClass:[NSHTTPURLResponse class]])
      self.status = (int)[(NSHTTPURLResponse*)response statusCode];
   handler(NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)task didReceiveData:(NSData*)data
{
   if (self.onData != nil && !self.onData((const char*)[data bytes], (size_t)[data length]))
   {
      self.failure = @"cancelled";
      [task cancel];
   }
}
- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error
{
   if (error != nil && self.failure == nil)
      self.failure = [error localizedDescription];
   dispatch_semaphore_signal(self.done);
}
@end

namespace AgentNet
{
   bool Post(const Request& request, const std::function<bool(const char*, size_t)>& onData, int& status, std::string& error)
   {
      status = 0;
      error.clear();
      @autoreleasepool
      {
         NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:request.url.c_str()]];
         if (url == nil || ([[url scheme] caseInsensitiveCompare:@"http"] != NSOrderedSame && [[url scheme] caseInsensitiveCompare:@"https"] != NSOrderedSame))
         {
            error = "the URL must start with http:// or https://";
            return false;
         }
         NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:url];
         [req setHTTPMethod:@"POST"];
         [req setTimeoutInterval:(NSTimeInterval)request.timeoutSeconds];
         for (const auto& h : request.headers)
            [req setValue:[NSString stringWithUTF8String:h.second.c_str()] forHTTPHeaderField:[NSString stringWithUTF8String:h.first.c_str()]];
         [req setHTTPBody:[NSData dataWithBytes:request.body.data() length:request.body.size()]];

         AgentNetDelegate* delegate = [AgentNetDelegate new];
         delegate.done = dispatch_semaphore_create(0);
         delegate.onData = ^BOOL(const char* p, size_t n) { return onData(p, n) ? YES : NO; };
         NSURLSessionConfiguration* config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
         config.timeoutIntervalForRequest = (NSTimeInterval)request.timeoutSeconds;
         config.timeoutIntervalForResource = 3600;
         NSOperationQueue* queue = [NSOperationQueue new];
         NSURLSession* session = [NSURLSession sessionWithConfiguration:config delegate:delegate delegateQueue:queue];
         NSURLSessionDataTask* task = [session dataTaskWithRequest:req];
         [task resume];
         dispatch_semaphore_wait(delegate.done, DISPATCH_TIME_FOREVER);
         [session finishTasksAndInvalidate];
         status = delegate.status;
         if (delegate.failure != nil)
         {
            error = std::string([delegate.failure UTF8String]);
            return false;
         }
         return true;
      }
   }

   static NSDictionary* Query(const std::string& service, const std::string& account)
   {
      return @{ (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
                (__bridge id)kSecAttrService : [NSString stringWithUTF8String:service.c_str()],
                (__bridge id)kSecAttrAccount : [NSString stringWithUTF8String:account.c_str()] };
   }

   bool SecretSet(const std::string& service, const std::string& account, const std::string& value)
   {
      SecItemDelete((__bridge CFDictionaryRef)Query(service, account));
      NSMutableDictionary* add = [Query(service, account) mutableCopy];
      add[(__bridge id)kSecValueData] = [NSData dataWithBytes:value.data() length:value.size()];
      return SecItemAdd((__bridge CFDictionaryRef)add, nullptr) == errSecSuccess;
   }

   bool SecretGet(const std::string& service, const std::string& account, std::string& value)
   {
      NSMutableDictionary* q = [Query(service, account) mutableCopy];
      q[(__bridge id)kSecReturnData] = @YES;
      q[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
      CFTypeRef out = nullptr;
      if (SecItemCopyMatching((__bridge CFDictionaryRef)q, &out) != errSecSuccess || out == nullptr)
         return false;
      NSData* data = (__bridge_transfer NSData*)out;
      value.assign((const char*)[data bytes], (size_t)[data length]);
      return true;
   }

   bool SecretDelete(const std::string& service, const std::string& account)
   {
      const OSStatus s = SecItemDelete((__bridge CFDictionaryRef)Query(service, account));
      return s == errSecSuccess || s == errSecItemNotFound;
   }

   bool SecretsAvailable() { return true; }
}
