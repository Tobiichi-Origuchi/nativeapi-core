// C++-only NativeActivity integration. Run only on a disposable Android emulator.
#include <android/log.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <atomic>
#include <functional>
#include <stdexcept>
#include <thread>
#include "../src/clipboard.h"
#include "../src/foundation/dispatcher.h"
#include "../src/image.h"
#include "../src/platform/android/android_runtime.h"

using namespace nativeapi;
namespace {
ANativeActivity* activity = nullptr;
int timer = -1;
int stage = 0;
int ticks = 0;
int events = 0;
int previous_events = 0;
size_t listener = 0;
bool read_done = false;
std::atomic<bool> worker_failed{false};
bool lost_focus = false;
bool regained_focus = false;
std::function<void()> next;
void Check(bool value, const char* label) {
  __android_log_print(value ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, "nativeapi-test", "%s %s",
                      value ? "PASS" : "FAIL", label);
  if (!value)
    throw std::runtime_error(label);
}
void Finish(bool success) {
  if (timer >= 0) {
    ALooper_removeFd(ALooper_forThread(), timer);
    close(timer);
    timer = -1;
  }
  __android_log_print(success ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, "nativeapi-test",
                      "NATIVEAPI_CPP_CLIPBOARD_%s", success ? "PASS" : "FAIL");
  ANativeActivity_finish(activity);
}
void ReadText(const std::string& expected) {
  read_done = false;
  Clipboard::GetInstance().ReadText([expected](bool ok, auto value) {
    try {
      Check(ok && value && *value == expected, "deferred text snapshot");
      read_done = true;
    } catch (...) {
      Finish(false);
    }
  });
  Check(!read_done, "read completes on later Looper turn");
}
void Focus(ANativeActivity*, int focused) {
  if (!focused)
    lost_focus = true;
  if (focused && lost_focus)
    regained_focus = true;
}
void LaunchWriter() {
  auto* env = activity->env;
  auto cls = env->FindClass("android/content/Intent");
  auto ctor = env->GetMethodID(cls, "<init>", "()V");
  auto intent = env->NewObject(cls, ctor);
  auto component = env->GetMethodID(
      cls, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;");
  auto package = android::ToJavaString(env, "dev.nativeapi.clipboard.cppwriter");
  auto name = android::ToJavaString(env, "android.app.NativeActivity");
  env->CallObjectMethod(intent, component, package, name);
  auto activity_class = env->GetObjectClass(activity->clazz);
  auto start = env->GetMethodID(activity_class, "startActivity", "(Landroid/content/Intent;)V");
  env->CallVoidMethod(activity->clazz, start, intent);
  Check(!android::ClearException(env), "launch independent native writer");
  env->DeleteLocalRef(activity_class);
  env->DeleteLocalRef(name);
  env->DeleteLocalRef(package);
  env->DeleteLocalRef(intent);
  env->DeleteLocalRef(cls);
}
int Tick(int fd, int, void*) {
  uint64_t count;
  if (read(fd, &count, sizeof(count)) <= 0)
    return 1;
  try {
    if (++ticks >= 100)
      Check(false, "test deadline");
    if (!android::IsActive())
      return 1;
    auto& board = Clipboard::GetInstance();
#ifdef NATIVEAPI_CLIPBOARD_WRITER
    if (stage++ == 0) {
      Check(board.WriteText("external 中文🙂"), "independent native write");
    } else {
      __android_log_print(ANDROID_LOG_INFO, "nativeapi-test", "NATIVEAPI_CPP_WRITER_PASS");
      ANativeActivity_finish(activity);
      ALooper_removeFd(ALooper_forThread(), timer);
      close(timer);
      timer = -1;
    }
#else
    switch (stage) {
      case 0:
        Check(board.IsSupported() && board.IsChangeMonitoringSupported(),
              "capabilities after native initialization");
        Check(board.Clear(), "clear");
        board.ReadText([](bool ok, auto text) {
          try {
            Check(ok && !text, "absent text");
            read_done = true;
          } catch (...) {
            Finish(false);
          }
        });
        ++stage;
        break;
      case 1:
        Check(read_done, "asynchronous read delivered");
        listener = board.AddListener<ClipboardChangedEvent>([](const auto&) { ++events; });
        Check(listener != 0 && board.IsMonitoring(), "first listener starts");
        ++stage;
        break;
      case 2:
        Check(events == 0, "no initial notification");
        Check(board.WriteText("中文🙂"), "unicode write");
        ReadText("中文🙂");
        ++stage;
        break;
      case 3: {
        Check(read_done && events > 0, "write notification");
        ClipboardData content;
        content.text = "fallback";
        content.html = "<b>中文🙂</b>";
        Check(board.Write(content), "text HTML write");
        read_done = false;
        board.Read([content](bool ok, ClipboardData data) {
          try {
            Check(ok && data.text == content.text && data.html == content.html && !data.image &&
                      data.file_paths.empty(),
                  "multi-format snapshot");
            read_done = true;
          } catch (...) {
            Finish(false);
          }
        });
        ++stage;
        break;
      }
      case 4: {
        Check(read_done, "HTML read delivered");
        auto image = Image::FromBase64(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAAH/"
            "iZk9HQAAAABJRU5ErkJggg==");
        Check(image && image->GetSize().width == 1 && !image->ToBase64().empty(),
              "native Bitmap PNG round trip");
        Check(!board.WriteImage(image) && !board.WriteFilePaths({"/tmp/private.txt"}),
              "unsupported publication fails explicitly");
        Check(board.WriteText(""), "empty text write");
        ReadText("");
        ++stage;
        break;
      }
      case 5:
        Check(read_done, "empty is present");
        board.RemoveListener(listener);
        Check(!board.IsMonitoring(), "last listener stops");
        previous_events = events;
        Check(board.WriteText("after stop"), "write after removal");
        ++stage;
        break;
      case 6:
        Check(events == previous_events, "no deliveries after removal");
        listener = board.AddListener<ClipboardChangedEvent>([](const auto&) { ++events; });
        Check(board.WriteText("baseline"), "resubscribe baseline");
        ++stage;
        break;
      case 7:
        Check(events > previous_events, "resubscribe notification");
        previous_events = events;
        lost_focus = regained_focus = false;
        LaunchWriter();
        ++stage;
        break;
      case 8:
        if (!regained_focus)
          break;
        if (events <= previous_events)
          break;
        Check(lost_focus && regained_focus, "focus loss and restoration");
        Check(events > previous_events, "external change on focus restoration");
        ReadText("external 中文🙂");
        ++stage;
        break;
      case 9:
        Check(read_done, "foreign clipboard text");
        board.RemoveListener(listener);
        read_done = false;
        std::thread([] {
          if (!Clipboard::GetInstance().WriteText("worker 中文🙂")) {
            worker_failed = true;
            return;
          }
          Clipboard::GetInstance().ReadText([](bool ok, auto value) {
            try {
              Check(ok && value && *value == "worker 中文🙂" && IsMainThread(),
                    "worker read/write marshals to host UI");
              read_done = true;
            } catch (...) {
              Finish(false);
            }
          });
        }).detach();
        ++stage;
        break;
      case 10:
        Check(!worker_failed, "worker write accepted");
        if (!read_done)
          break;
        Check(board.Clear(), "final clear");
        Finish(true);
        break;
    }
#endif
  } catch (...) {
    Finish(false);
  }
  return timer >= 0 ? 1 : 0;
}
}  // namespace
extern "C" __attribute__((visibility("default"))) void
ANativeActivity_onCreate(ANativeActivity* host, void*, size_t) {
  activity = host;
  try {
    Check(!Clipboard::IsSupported(), "no support before host initialization");
    activity->callbacks->onWindowFocusChanged = Focus;
    Check(android::Initialize(activity), "native host initialization");
    timer = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    Check(timer >= 0, "test timer");
    Check(ALooper_addFd(ALooper_forThread(), timer, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT,
                        Tick, nullptr) == 1,
          "test Looper source");
    itimerspec interval{};
    interval.it_value.tv_nsec = interval.it_interval.tv_nsec = 250000000;
    Check(timerfd_settime(timer, 0, &interval, nullptr) == 0, "start test timer");
  } catch (...) {
    Finish(false);
  }
}
