// Explicit simulator integration test. Run only in a disposable simulator.
#import <UIKit/UIKit.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include "../src/clipboard.h"
#include "../src/image.h"
using namespace nativeapi;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  if (!ok)
    throw std::runtime_error(label);
}
void Later(double delay, std::function<void()> fn) {
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay * NSEC_PER_SEC), dispatch_get_main_queue(),
                 ^{
                   fn();
                 });
}
@interface Delegate : UIResponder <UIApplicationDelegate>
@property(strong, nonatomic) UIWindow* window;
@end
@implementation Delegate
- (BOOL)application:(UIApplication*)app didFinishLaunchingWithOptions:(NSDictionary*)options {
  self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  self.window.rootViewController = [[UIViewController alloc] init];
  [self.window makeKeyAndVisible];
  Later(1, [] {
    auto& b = Clipboard::GetInstance();
    Check(b.IsSupported() && b.IsChangeMonitoringSupported(), "capabilities");
    Check(b.Clear(), "clear");
    b.ReadText([](bool ok, auto text) { Check(ok && !text, "absent text"); });
    Later(.2, [] {
      auto& b = Clipboard::GetInstance();
      static int events = 0;
      static size_t listener = b.AddListener<ClipboardChangedEvent>([](const auto&) { ++events; });
      Later(.2, [] {
        Check(events == 0 && Clipboard::GetInstance().IsMonitoring(), "no initial notification");
        Check(Clipboard::GetInstance().WriteText("中文🙂"), "unicode write");
        Clipboard::GetInstance().ReadText(
            [](bool ok, auto text) { Check(ok && text == "中文🙂", "unicode read"); });
        Later(.3, [] {
          Check(events > 0, "change notification");
          ClipboardData data;
          data.text = "fallback";
          data.html = "<b>中文🙂</b>";
          data.image = Image::FromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4n"
                                         "GP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg==");
          Check(data.image && data.image->GetSize().width == 1, "image decode");
          data.file_paths = {"/tmp/nativeapi-a.txt", "/tmp/nativeapi-b.txt"};
          Check(Clipboard::GetInstance().Write(data), "multi-format write");
          Clipboard::GetInstance().Read([](bool ok, auto content) {
            Check(ok && content.text == "fallback" && content.html == "<b>中文🙂</b>",
                  "text HTML snapshot");
            Check(content.image && content.image->GetSize().width == 1, "image snapshot");
            Check(content.file_paths.size() == 2, "file URLs snapshot");
            auto& b = Clipboard::GetInstance();
            b.RemoveListener(listener);
            Check(!b.IsMonitoring(), "last listener stops");
            int previous = events;
            Check(b.WriteText(""), "empty text write");
            b.ReadText([](bool ok, auto text) {
              Check(ok && text && text->empty(), "empty differs from absent");
            });
            Later(.3, [previous] {
              Check(events == previous, "no events after removal");
              listener = Clipboard::GetInstance().AddListener<ClipboardChangedEvent>(
                  [](const auto&) { ++events; });
              Check(Clipboard::GetInstance().Clear(), "clear after resubscribe");
              Later(.3, [previous] {
                Check(events > previous, "resubscribe notification");
                Clipboard::GetInstance().RemoveListener(listener);
                std::cout << "NATIVEAPI_IOS_CLIPBOARD_PASS" << std::endl;
                exit(0);
              });
            });
          });
        });
      });
    });
  });
  return YES;
}
@end
int main(int argc, char** argv) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil, NSStringFromClass(Delegate.class));
  }
}
