#include "ios.hh"

#if ORO_RUNTIME_PLATFORM_IOS

#import <UIKit/UIKit.h>

#include "../app.hh"
#include "../core/services/otp.hh"
#include "../string.hh"
#include <cstring>

using oro::runtime::app::App;
using oro::runtime::types::String;
using oro::runtime::core::services::OTP;

@interface OROOTPTextFieldCoordinator : NSObject <UITextFieldDelegate>
@property (nonatomic, assign) uint64_t requestId;
@property (nonatomic, assign) OTP* service;
@property (nonatomic, copy) NSString* expectedAnchor;
@property (nonatomic, strong) UIView* container;
@property (nonatomic, strong) UITextField* textField;
@property (nonatomic, strong) id textObserver;
- (instancetype)initWithService:(OTP*)service requestId:(uint64_t)identifier;
- (BOOL)startWithHost:(NSString*)host;
- (void)teardown;
@end

@implementation OROOTPTextFieldCoordinator

- (instancetype)initWithService:(OTP*)service requestId:(uint64_t)identifier {
  self = [super init];
  if (self) {
    _service = service;
    _requestId = identifier;
  }
  return self;
}

- (UIWindow*)keyWindow {
  UIApplication* application = UIApplication.sharedApplication;
  if (@available(iOS 13.0, *)) {
    for (UIScene* scene in application.connectedScenes) {
      if (scene.activationState != UISceneActivationStateForegroundActive) {
        continue;
      }
      if (![scene isKindOfClass:[UIWindowScene class]]) {
        continue;
      }
      UIWindowScene* windowScene = (UIWindowScene*)scene;
      for (UIWindow* window in windowScene.windows) {
        if (window.isHidden) {
          continue;
        }
        if (window.isKeyWindow) {
          return window;
        }
      }
      if (windowScene.windows.count > 0) {
        return windowScene.windows.firstObject;
      }
    }
  }
  UIWindow* window = application.keyWindow;
  if (!window && application.windows.count > 0) {
    window = application.windows.firstObject;
  }
  return window;
}

- (BOOL)startWithHost:(NSString*)host {
  UIWindow* window = [self keyWindow];
  if (!window) {
    return NO;
  }

  UIView* container = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 1, 1)];
  container.alpha = 0.01;
  container.userInteractionEnabled = NO;
  container.isAccessibilityElement = NO;
  container.translatesAutoresizingMaskIntoConstraints = NO;

  UITextField* field = [[UITextField alloc] initWithFrame:CGRectMake(0, 0, 1, 1)];
  field.textContentType = UITextContentTypeOneTimeCode;
  field.keyboardType = UIKeyboardTypeNumberPad;
  field.autocorrectionType = UITextAutocorrectionTypeNo;
  field.autocapitalizationType = UITextAutocapitalizationTypeNone;
  field.secureTextEntry = NO;
  field.delegate = self;
  field.accessibilityLabel = host;
  [field addTarget:self action:@selector(textDidChange:) forControlEvents:UIControlEventEditingChanged];

  [container addSubview:field];
  [window addSubview:container];

  self.container = container;
  self.textField = field;
  self.expectedAnchor = [NSString stringWithFormat:@"@%@", host];

  if (@available(iOS 11.0, *)) {
    [field setContentCompressionResistancePriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
    [field setContentCompressionResistancePriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisVertical];
  }

  [container.topAnchor constraintEqualToAnchor:window.topAnchor].active = YES;
  [container.leadingAnchor constraintEqualToAnchor:window.leadingAnchor].active = YES;

  dispatch_async(dispatch_get_main_queue(), ^{
    [self.textField becomeFirstResponder];
  });

  return YES;
}

- (void)teardown {
  if (self.textField.isFirstResponder) {
    [self.textField resignFirstResponder];
  }
  if (self.textField) {
    [self.textField removeTarget:self action:@selector(textDidChange:) forControlEvents:UIControlEventEditingChanged];
  }
  [self.container removeFromSuperview];
  self.textField = nil;
  self.container = nil;
  self.expectedAnchor = nil;
  self.service = nullptr;
}

- (void)textDidChange:(UITextField*)textField {
  NSString* trimmed = [textField.text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
  if (trimmed.length == 0) {
    return;
  }

  OTP* service = self.service;
  if (!service) {
    return;
  }

  if (self.expectedAnchor.length > 0) {
    std::string message;
    const char* anchorCString = self.expectedAnchor.UTF8String;
    const char* codeCString = trimmed.UTF8String;

    if (anchorCString && codeCString) {
      message.reserve(strlen(anchorCString) + strlen(codeCString) + 2);
      message.append(anchorCString);
      message.append(" #");
      message.append(codeCString);

      oro::runtime::types::String synthesized(message.c_str());
      service->handleMessage(self.requestId, synthesized);
    } else {
      oro::runtime::types::String code(codeCString ? codeCString : "");
      service->handleCode(self.requestId, code);
    }
  } else {
    oro::runtime::types::String code(trimmed.UTF8String ? trimmed.UTF8String : "");
    service->handleCode(self.requestId, code);
  }
  oro::runtime::core::services::stopIOSOTPRequest(self.requestId);
}

@end

namespace {
  NSMutableDictionary<NSNumber*, OROOTPTextFieldCoordinator*>* Coordinators() {
    static NSMutableDictionary<NSNumber*, OROOTPTextFieldCoordinator*>* dictionary = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
      dictionary = [NSMutableDictionary dictionary];
    });
    return dictionary;
  }
}

namespace oro::runtime::core::services {

bool startIOSOTPRequest(uint64_t id, OTP& service, uint64_t timeoutMs, const String& host) {
  if (@available(iOS 12.0, *)) {
    __block BOOL started = NO;
    NSString* hostString = [NSString stringWithUTF8String:host.c_str()];
    dispatch_sync(dispatch_get_main_queue(), ^{
      NSNumber* key = @(id);
      if (Coordinators()[key] != nil) {
        started = YES;
        return;
      }

      OROOTPTextFieldCoordinator* coordinator = [[OROOTPTextFieldCoordinator alloc] initWithService:&service requestId:id];
      if (![coordinator startWithHost:hostString]) {
  #if !__has_feature(objc_arc)
        [coordinator release];
  #endif
        return;
      }

      Coordinators()[key] = coordinator;
  #if !__has_feature(objc_arc)
      [coordinator release];
  #endif
      started = YES;
    });
    (void)timeoutMs;
    return started;
  }

  return false;
}

void stopIOSOTPRequest(uint64_t id) {
  if (!@available(iOS 12.0, *)) {
    return;
  }

  dispatch_async(dispatch_get_main_queue(), ^{
    NSNumber* key = @(id);
    OROOTPTextFieldCoordinator* coordinator = Coordinators()[key];
    if (!coordinator) {
      return;
    }
    [coordinator teardown];
    [Coordinators() removeObjectForKey:key];
  });
}

}

#endif
