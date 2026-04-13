#ifndef ORO_RUNTIME_WEBVIEW_WEBVIEW_H
#define ORO_RUNTIME_WEBVIEW_WEBVIEW_H

#include "../unique_client.hh"
#include "../platform.hh"
#include "../options.hh"
#include "../config.hh"
#include "../http.hh"
#include "../json.hh"

namespace oro::runtime::webview {
  class Navigator;
}

#if ORO_RUNTIME_PLATFORM_APPLE
@interface ORONavigationDelegate : NSObject<WKNavigationDelegate>
@property (nonatomic) oro::runtime::webview::Navigator* _Nullable navigator;
-    (void) webView: (WKWebView* _Nonnull) webView
  didFailNavigation: (WKNavigation* _Nullable) navigation
          withError: (NSError* _Nonnull) error;

-               (void) webView: (WKWebView* _Nonnull) webView
  didFailProvisionalNavigation: (WKNavigation* _Nullable) navigation
                     withError: (NSError* _Nonnull) error;

-                  (void) webView: (WKWebView* _Nonnull) webview
  decidePolicyForNavigationAction: (WKNavigationAction* _Nonnull) navigationAction
                  decisionHandler: (void (^ _Nonnull)(WKNavigationActionPolicy)) decisionHandler;

-                    (void) webView: (WKWebView* _Nonnull) webView
  decidePolicyForNavigationResponse: (WKNavigationResponse* _Nonnull) navigationResponse
                    decisionHandler: (void (^ _Nonnull)(WKNavigationResponsePolicy)) decisionHandler;

-      (void) webView: (WKWebView* _Nonnull) webView
  didFinishNavigation: (WKNavigation* _Nullable) navigation;

- (void) webView: (WKWebView* _Nonnull) webView
didReceiveAuthenticationChallenge: (NSURLAuthenticationChallenge* _Nonnull) challenge
 completionHandler: (void (^ _Nonnull)(NSURLSessionAuthChallengeDisposition disposition, NSURLCredential* _Nullable credential)) completionHandler;
@end
@interface OROWebView :
#if ORO_RUNTIME_PLATFORM_IOS
  WKWebView<WKUIDelegate>
  @property (strong, nonatomic) NSLayoutConstraint* _Nullable keyboardHeightConstraint;
  @property (strong, nonatomic) UIRefreshControl* _Nullable refreshControl;
  - (void) handlePullToRefresh: (UIRefreshControl* _Nonnull) refreshControl;
  - (instancetype _Nonnull) initWithFrame: (CGRect) frame
                   configuration: (WKWebViewConfiguration* _Nonnull) configuration
       withRefreshControlEnabled: (BOOL) refreshControlEnabled;
#else
  WKWebView<
    WKUIDelegate,
    NSDraggingDestination,
    NSFilePromiseProviderDelegate,
    NSDraggingSource
  >

  @property (nonatomic) NSPoint initialWindowPos;
  @property (nonatomic) CGFloat contentHeight;
  @property (nonatomic) CGFloat radius;
  @property (nonatomic) CGFloat margin;
  @property (nonatomic) BOOL shouldDrag;
#endif

#if ORO_RUNTIME_PLATFORM_MACOS
- (instancetype) initWithFrame: (NSRect) frameRect
                 configuration: (WKWebViewConfiguration*) configuration
                        radius: (CGFloat) radius
                        margin: (CGFloat) margin;

  -   (NSDragOperation) draggingSession: (NSDraggingSession *) session
  sourceOperationMaskForDraggingContext: (NSDraggingContext) context;

  -             (void) webView: (WKWebView* _Nonnull) webView
    runOpenPanelWithParameters: (WKOpenPanelParameters* _Nonnull) parameters
              initiatedByFrame: (WKFrameInfo* _Nonnull) frame
             completionHandler: (void (^ _Nonnull)(NSArray<NSURL*>* _Nullable)) completionHandler;
#endif

#if ORO_RUNTIME_PLATFORM_MACOS || (ORO_RUNTIME_PLATFORM_IOS && __IPHONE_OS_VERSION_MIN_REQUIRED >= __IPHONE_15)

  -                                      (void) webView: (WKWebView* _Nonnull) webView
   requestDeviceOrientationAndMotionPermissionForOrigin: (WKSecurityOrigin* _Nonnull) origin
                                       initiatedByFrame: (WKFrameInfo* _Nonnull) frame
                                        decisionHandler: (void (^ _Nonnull)(WKPermissionDecision decision)) decisionHandler;

  -                        (void) webView: (WKWebView* _Nonnull) webView
   requestMediaCapturePermissionForOrigin: (WKSecurityOrigin* _Nonnull) origin
                         initiatedByFrame: (WKFrameInfo* _Nonnull) frame
                                     type: (WKMediaCaptureType) type
                          decisionHandler: (void (^ _Nonnull)(WKPermissionDecision decision)) decisionHandler;
#endif

  -                     (void) webView: (WKWebView* _Nonnull) webView
    runJavaScriptAlertPanelWithMessage: (NSString* _Nonnull) message
                      initiatedByFrame: (WKFrameInfo* _Nonnull) frame
                     completionHandler: (void (^ _Nonnull)(void)) completionHandler;

  -                       (void) webView: (WKWebView* _Nonnull) webView
    runJavaScriptConfirmPanelWithMessage: (NSString* _Nonnull) message
                        initiatedByFrame: (WKFrameInfo* _Nonnull) frame
                       completionHandler: (void (^ _Nonnull)(BOOL result)) completionHandler;
@end

#if ORO_RUNTIME_PLATFORM_IOS
@interface OROWebViewController : UIViewController<UIGestureRecognizerDelegate>
  @property (nonatomic, strong) OROWebView* _Nullable webview;

-                         (BOOL) gestureRecognizer: (UIGestureRecognizer* _Nonnull) gestureRecognizer
shouldRecognizeSimultaneouslyWithGestureRecognizer: (UIGestureRecognizer* _Nonnull) otherGestureRecognizer;

- (BOOL) gestureRecognizer: (UIGestureRecognizer* _Nonnull) gestureRecognizer
        shouldReceiveTouch: (UITouch* _Nonnull) touch;
@end

#endif
#endif

namespace oro::runtime::webview {
  using Headers = http::Headers;

#if ORO_RUNTIME_PLATFORM_ANDROID
  class CoreAndroidWebView;
  class CoreAndroidWebViewSettings;
#endif

#if ORO_RUNTIME_PLATFORM_APPLE
  using WebView = OROWebView;
  using WebViewSettings = WKWebViewConfiguration;
#elif ORO_RUNTIME_PLATFORM_LINUX && !ORO_RUNTIME_DESKTOP_EXTENSION
  using WebView = WebKitWebView;
  using WebViewSettings = WebKitSettings;
#elif ORO_RUNTIME_PLATFORM_WINDOWS
  using WebView = ICoreWebView2;
  using WebViewSettings = Microsoft::WRL::ComPtr<CoreWebView2EnvironmentOptions>;
#elif ORO_RUNTIME_PLATFORM_ANDROID
  using WebView = CoreAndroidWebView;
  using WebViewSettings = CoreAndroidWebViewSettings;
#else
  struct WebView;
  struct WebViewSettings;
#endif
}
#endif
