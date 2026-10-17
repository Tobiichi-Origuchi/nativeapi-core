// Integration tests run only inside the disposable HarmonyOS emulator HAP.
#include <deviceinfo.h>
#include <hilog/log.h>
#include <napi/native_api.h>
#include <uv.h>
#include <window_manager/oh_display_manager.h>
#include <atomic>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <thread>
#include "../../../../../../src/capi/device_info_c.h"
#include "../../../../../../src/capi/message_dialog_c.h"
#include "../../../../../../src/capi/string_utils_c.h"
#include "../../../../../../src/capi/window_c.h"
#include "../../../../../../src/capi/window_manager_c.h"
#include "../../../../../../src/clipboard.h"
#include "../../../../../../src/device_info.h"
#include "../../../../../../src/display_manager.h"
#include "../../../../../../src/foundation/dispatcher.h"
#include "../../../../../../src/foundation/event_request.h"
#include "../../../../../../src/message_dialog.h"
#include "../../../../../../src/platform/ohos/ohos_runtime.h"
#include "../../../../../../src/platform/ohos/ohos_window.h"
#include "../../../../../../src/preferences.h"
#include "../../../../../../src/window_manager.h"

#include "picker_tests.h"
#include "service_tests.h"
#include "url_tests.h"
#include "notification_tests.h"
#include "view_tests.h"
#include "tooltip_tests.h"
#include "menu_tests.h"
#include "accessibility_tests.h"
#include "application_tests.h"
#include "tray_tests.h"
#include "app_integration_tests.h"
#include "occlusion_tests.h"
#include "window_style_tests.h"
#include "window_shape_tests.h"
#include "window_drag_tests.h"
#include "window_controls_tests.h"
#include "window_title_tests.h"
#include "window_fullscreen_tests.h"
#include "window_mode_tests.h"
#include "window_layout_tests.h"
#include "drag_session_probe.h"
#include "window_aspect_tests.h"
#include "cursor_tests.h"
#include "display_test_dispatcher.h"
#include "display_tests.h"
#include "clipboard_interop_tests.h"
#include "clipboard_boundary_tests.h"
#include "shortcut_tests.h"
#include "startup_tests.h"
#include "device_info_tests.h"
#include "preferences_tests.h"
#include "foundation_tests.h"
#include "window_shadow_probe.h"
#include "window_shadow_tests.h"
#include "window_manager_tests.h"
#include "window_parent_tests.h"
#include "keyboard_monitor_tests.h"
#include "drop_tests.h"
#include "drag_source_tests.h"
#include "window_native_close_tests.h"

using namespace nativeapi;
namespace {
bool uninitialized_closed = false;
class Tests : public std::enable_shared_from_this<Tests> {
 public:
  napi_env env;
  napi_deferred deferred;
  uv_timer_t timer{};
  int passed = 0, failed = 0, ticks = 0;
  int stage = 0;
  size_t listener = 0;
  int changes = 0;
  bool done = false;
  std::ostringstream report;
  std::shared_ptr<Window> main_window, child, duplicate;
  std::shared_ptr<Image> clipboard_image;
  std::vector<std::string> clipboard_files;
  std::shared_ptr<EventDecision> close_decision;
  size_t manager_listener = 0, object_listener = 0, duplicate_listener = 0, close_listener = 0;
  std::map<std::string, int> manager_events, object_events;
  int window_phase = 0, phase_ticks = 0, close_requests = 0, failed_creations = 0;
  size_t abort_creation_listener = 0;
  int entered_before = 0, exited_before = 0;
  bool event_threads_ok = true;
  bool desktop = false;
  Rectangle child_target{40, 100, 280, 220};
  Point overlap{50, 110};
  int maximized_before = 0, restored_before = 0, minimized_before = 0;
  std::shared_ptr<MessageDialog> dialog, nested_dialog;
  std::shared_ptr<Window> dialog_other;
  size_t dialog_snapshot_listener = 0;
  bool late_snapshot = false, late_focusable = false, late_ignore_mouse = false;
  int dialog_phase = 0, dialog_ticks = 0;
  native_message_dialog_t c_dialog = NATIVE_INVALID_MESSAGE_DIALOG;
  std::atomic<bool> modal_done{false}, modal_ok{false}, nested_done{false}, nested_ok{false};

  void Check(bool ok, const char* label) {
    (ok ? passed : failed)++;
    report << (ok ? "PASS " : "FAIL ") << label << '\n';
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 0x0001, "nativeapi-test",
                 "%{public}s %{public}s", ok ? "PASS" : "FAIL", label);
  }
  void Begin() {
    Check(uninitialized_closed, "uninitialized host rejects clipboard access");
    Check(IsMainThread() && IsMainThreadDispatchSupported(), "ArkTS UI dispatcher initialized");
    auto& device = DeviceInfo::GetInstance();
    Check(!device.GetName().empty(), "user device name or public NDK market fallback available");
    Check(device.GetModel() == OH_GetProductModel(), "product model from SDK");
    Check(device.GetManufacturer() == OH_GetManufacture(), "manufacturer from SDK");
    Check(!device.GetOsName().empty() && !device.GetOsVersion().empty(),
          "OS distribution/version available");
#if defined(__aarch64__)
    constexpr const char* expected_architecture = "aarch64";
#elif defined(__x86_64__)
    constexpr const char* expected_architecture = "x86_64";
#else
#error "This integration test supports ARM64 and x86_64 emulator builds"
#endif
    Check(!device.GetKernelVersion().empty() && device.GetArchitecture() == expected_architecture,
          "kernel/CPU architecture");
    char* model = native_device_info_get_model();
    Check(model && device.GetModel() == model, "C ABI device model");
    free_c_str(model);
    report << device.GetOsName() << ' ' << device.GetOsVersion() << ' ' << device.GetModel()
           << '\n';

    auto& manager = DisplayManager::GetInstance();
    auto display = manager.GetPrimary();
    Check(display != nullptr && !manager.GetAll().empty(), "enumerate actual primary display");
    if (display) {
      Check(manager.GetPrimary() == display, "display identity stable across enumeration");
      NativeDisplayManager_DisplayInfo* info = nullptr;
      OH_NativeDisplayManager_CreatePrimaryDisplay(&info);
      Check(
          info &&
              std::abs(display->GetSize().width * display->GetScaleFactor() - info->width) < 0.01 &&
              std::abs(display->GetSize().height * display->GetScaleFactor() - info->height) < 0.01,
          "logical display geometry matches SDK pixels/density");
      Check(info && display->GetName() == info->name &&
                display->GetRefreshRate() == static_cast<int>(info->refreshRate),
            "display name/refresh rate match SDK");
      Check(display->IsPrimary() && display->GetNativeObject(),
            "primary display has live native token");
      const auto area = display->GetWorkArea();
      Check(area.width > 0 && area.height > 0, "available work area queried");
      OH_NativeDisplayManager_DestroyDisplay(info);
    }
    {
      Preferences p("ohos-smoke/隔离");
      Check(p.Clear(), "preferences clear");
      Check(p.Set("unicode", "中文🙂") && p.Set("empty", ""), "preferences Unicode/empty writes");
      Check(p.Get("unicode") == "中文🙂" && p.Get("empty", "fallback").empty(),
            "preferences string reads");
      Check(p.Get("missing", "fallback") == "fallback" && !p.Contains("missing"),
            "missing preference defaults");
      Check(p.Contains("empty") && p.GetAll().size() == 2 && p.GetSize() == 2 &&
                p.GetKeys().size() == 2,
            "preferences enumeration and membership");
      Check(!p.Set(std::string("bad\0key", 7), "value"), "reject embedded NUL preference keys");
    }
    {
      Preferences p("ohos-smoke/隔离"), isolated("ohos-smoke-isolated");
      Check(p.Get("unicode") == "中文🙂", "preferences persist after close/reopen");
      Check(!isolated.Contains("unicode"), "preferences scope isolation");
      Check(p.Remove("unicode") && !p.Contains("unicode") && p.GetSize() == 1,
            "preferences remove");
      Check(p.Clear() && p.GetAll().empty(), "preferences persisted clear");
    }
    {
      Preferences persistent("ohos-smoke-process-persistence");
      const auto prior = persistent.Get("marker");
      if (prior.empty())
        Check(persistent.Set("marker", "persisted 中文🙂"), "seed cross-process preference");
      else
        Check(prior == "persisted 中文🙂", "preferences survive process restart");
    }

    nativeapi::tests::Services([this](bool ok, const char* label) { Check(ok, label); });

    auto& clipboard = Clipboard::GetInstance();
    Check(clipboard.IsSupported() && clipboard.IsChangeMonitoringSupported(),
          "clipboard capabilities");
    listener = clipboard.AddListener<ClipboardChangedEvent>([weak = weak_from_this()](const auto&) {
      if (auto self = weak.lock()) {
        self->Check(IsMainThread(), "clipboard notification on UI thread");
        self->changes++;
      }
    });
    Check(clipboard.IsMonitoring() && changes == 0, "listener starts without initial event");
    Check(clipboard.Clear(), "clipboard clear");
    auto self = shared_from_this();
    auto returned = std::make_shared<bool>(false);
    clipboard.ReadText([self, returned](bool ok, auto text) {
      self->Check(*returned && IsMainThread(), "clipboard read deferred to UI turn");
      self->Check(ok && !text, "empty clipboard is successful absence");
      self->TextRoundTrip();
    });
    *returned = true;
  }
  void TextRoundTrip() {
    auto& clipboard = Clipboard::GetInstance();
    Check(clipboard.WriteText("中文🙂"), "clipboard Unicode write");
    clipboard.ReadText([self = shared_from_this()](bool ok, auto text) {
      self->Check(ok && text && *text == "中文🙂", "clipboard Unicode round trip");
      self->HtmlRoundTrip();
    });
  }
  void HtmlRoundTrip() {
    ClipboardData value;
    value.text = "plain";
    value.html = "<b>中文🙂</b>";
    auto& clipboard = Clipboard::GetInstance();
    Check(clipboard.Write(value), "clipboard multi-format write");
    clipboard.Read([self = shared_from_this(), value](bool ok, auto result) {
      self->Check(ok && result.text == value.text && result.html == value.html,
                  "clipboard plain text/HTML round trip");
      auto& clipboard = Clipboard::GetInstance();
      self->Check(!clipboard.WriteText(std::string("bad\0text", 8)),
                  "reject invalid clipboard strings");
      self->Check(!clipboard.WriteFilePaths({"/data/storage/el2/base/files/unsupported"}),
                  "unreadable file publication fails before replacing clipboard");
      clipboard.ReadText([self](bool ok, auto text) {
        self->Check(ok && text && *text == "plain", "failed writes preserve clipboard");
        self->ClipboardFormats();
      });
    });
  }
  void ClipboardFormats() {
    auto& clipboard = Clipboard::GetInstance();
    Check(clipboard.WriteImage(tests::FixtureImage()),
          "clipboard publishes a real native PixelMap");
    clipboard.ReadImage([self = shared_from_this()](bool ok, auto image) {
      self->Check(ok && tests::FixturePixels(image),
                  "clipboard image read preserves actual RGBA pixels");
      self->clipboard_image = image;
      auto& clipboard = Clipboard::GetInstance();
      self->Check(
          clipboard.Clear() && tests::FixturePixels(image),
          "clipboard images retain independent pixels after the native clipboard is cleared");
      self->clipboard_files = {"/data/storage/el2/base/files/nativeapi-clipboard-中文 space+#%.png",
                               "/data/storage/el2/base/files/nativeapi-clipboard-file.txt"};
      const auto& paths = self->clipboard_files;
      auto source = tests::FixtureImage();
      std::ofstream file(paths[1]);
      file << "NativeAPI test file 中文🙂";
      file.close();
      self->Check(source && source->SaveToFile(paths[0]) && !file.fail(),
                  "clipboard sandbox test files are real readable files");
      self->Check(clipboard.WriteFilePaths(paths),
                  "clipboard publishes sandbox-aware native file URIs");
      clipboard.ReadFilePaths([self](bool ok, auto result) {
        self->Check(ok && result == self->clipboard_files,
                    "clipboard file paths round trip in order with URI escaping and Unicode");
        ClipboardData combined;
        combined.text = "all formats";
        combined.html = "<b>all formats</b>";
        combined.image = self->clipboard_image;
        combined.file_paths = self->clipboard_files;
        auto& clipboard = Clipboard::GetInstance();
        self->Check(clipboard.Write(combined),
                    "clipboard replaces all four formats in one native data package");
        clipboard.Read([self, combined](bool ok, auto result) {
          self->Check(ok && result.text == combined.text && result.html == combined.html &&
                          result.file_paths == combined.file_paths &&
                          tests::FixturePixels(result.image),
                      "clipboard combined text HTML image and files all survive a single read");
          auto& clipboard = Clipboard::GetInstance();
          self->Check(
              !clipboard.WriteFilePaths({self->clipboard_files[0], "/no-such-dir/missing-file"}),
              "clipboard rejects an unreadable mixed file list before publication");
          clipboard.Read([self, combined](bool ok, auto result) {
            self->Check(
                ok && result.text == combined.text && result.file_paths == combined.file_paths &&
                    tests::FixturePixels(result.image),
                "clipboard failed file conversion preserves all previously published formats");
            for (const auto& path : self->clipboard_files)
              unlink(path.c_str());
            self->Worker();
          });
        });
      });
    });
  }
  void Worker() {
    auto self = shared_from_this();
    std::thread([self] {
      const bool metadata = AppInfo::GetInstance().GetIdentifier() == "dev.nativeapi.smoke" &&
                            AppInfo::GetInstance().GetName() == "NativeAPI Smoke";
      const bool secret = SecureStorage("ohos-smoke-secure-process-persistence").Get("marker") ==
                          "persisted secret 中文🙂";
      const bool written = Clipboard::GetInstance().WriteText("worker");
      RunOnMainThread([self, written, metadata, secret] {
        self->Check(metadata,
                    "worker metadata getters marshal through the initialized UI environment");
        self->Check(secret, "worker reads use the actual system encrypted storage service");
        self->Check(written && IsMainThread(), "worker writes marshal to UI thread");
        Clipboard::GetInstance().ReadText([self](bool ok, auto text) {
          self->Check(ok && text && *text == "worker", "worker clipboard readback");
          self->stage = 1;
        });
      });
    }).detach();
  }
  void Tick() {
    if (++ticks >= (stage == 4 ? 900 : (desktop ? 300 : 150))) {
      OH_LOG_Print(LOG_APP, LOG_ERROR, 0x0001, "nativeapi-test",
                   "timeout at window phase %{public}d (desktop %{public}d)", window_phase,
                   desktop);
      Check(false, "test deadline");
      Finish();
    } else if (stage == 4) {
      DialogTick();
    } else if (stage == 3) {
      WindowTick();
    } else if (stage == 1 && changes > 0) {
      auto& clipboard = Clipboard::GetInstance();
      Check(true, "native clipboard change notification received");
      clipboard.RemoveListener(listener);
      listener = 0;
      Check(!clipboard.IsMonitoring(), "last listener stops monitoring");
      Check(clipboard.Clear(), "final clipboard cleanup");
      clipboard.ReadText([self = shared_from_this()](bool ok, auto text) {
        self->Check(ok && !text, "clear removes all clipboard formats");
        self->BeginWindows();
      });
      stage = 2;
    }
  }
  void BeginWindows() {
    stage = 3;
    ticks = 0;
    auto& manager = WindowManager::GetInstance();
    main_window = manager.GetCurrent();
    Check(main_window && main_window->GetNativeObject(), "WindowStage main window attached");
    if (!main_window) {
      Finish();
      return;
    }
    desktop = std::string(OH_GetDeviceType()) == "2in1";
    const auto main_bounds = main_window->GetBounds();
    child_target.x = main_bounds.x + 40;
    child_target.y = main_bounds.y + 100;
    overlap = {child_target.x + 10, child_target.y + 10};
    Check(manager.Get(main_window->GetId()) == main_window && manager.GetAll().size() == 1,
          "main window identity and enumeration");
    Window empty, unknown(reinterpret_cast<void*>(1));
    Check(empty.GetId() == IdAllocator::kInvalidId && !empty.GetNativeObject() &&
              !unknown.GetNativeObject(),
          "unattached window tokens fail safely");
    Check(!main_window->Close(), "Ability-owned main window is not destroyed by Close");
    auto c_window = native_window_manager_get_current();
    const auto c_bounds = native_window_get_bounds(c_window);
    Check(c_window && native_window_get_id(c_window) == main_window->GetId() && c_bounds.width > 0,
          "generated C ABI exposes the attached main window");
    native_window_free(c_window);
    manager_listener =
        manager.AddListener<WindowEvent>([weak = weak_from_this()](const WindowEvent& event) {
          if (auto self = weak.lock()) {
            self->event_threads_ok &= IsMainThread();
            ++self->manager_events[event.GetTypeName()];
          }
        });
    Check(!ohos::CreateSubWindow("", "pages/Child", [](auto) {}),
          "invalid child creation is rejected");
    const bool submitted = ohos::CreateSubWindow(
        "NativeAPI_smoke_child", "pages/Child",
        [self = shared_from_this()](std::shared_ptr<Window> window) {
          self->Check(window != nullptr && IsMainThread(),
                      "asynchronous native child creation and content load");
          if (!window) {
            self->Finish();
            return;
          }
          self->child = window;
          self->duplicate = std::make_shared<Window>(window->GetNativeObject());
          self->Check(self->duplicate->GetId() == window->GetId(),
                      "wrappers share one native window identity");
          self->Check(WindowManager::GetInstance().Get(window->GetId()) == window &&
                          WindowManager::GetInstance().GetAll().size() == 2,
                      "created child is registered exactly once");
          self->Check(window->GetParentWindow() == self->main_window,
                      "child parent identity is preserved");
          self->object_listener = window->AddListener<WindowEvent>(
              [weak = self->weak_from_this()](const WindowEvent& event) {
                if (auto current = weak.lock()) {
                  current->event_threads_ok &= IsMainThread();
                  ++current->object_events[event.GetTypeName()];
                }
              });
          self->duplicate_listener = self->duplicate->AddListener<WindowResizedEvent>(
              [weak = self->weak_from_this()](const WindowResizedEvent& event) {
                if (auto current = weak.lock()) {
                  current->Check(event.GetWindowId() == current->child->GetId(),
                                 "duplicate wrapper receives native resize identity");
                }
              });
          window->SetBounds(self->child_target);
          window->SetFocusable(false);
          self->Check(!window->IsFocusable(), "native focusable property can be disabled");
          self->Check(window->SetIgnoreMouseEvents(true, false) && window->IsIgnoreMouseEvents(),
                      "native touchability is mapped to ignored input");
          self->Check(!window->SetIgnoreMouseEvents(false, true) && window->IsIgnoreMouseEvents(),
                      "unsupported input forwarding preserves native touchability");
          self->Check(window->SetIgnoreMouseEvents(false, false) && !window->IsIgnoreMouseEvents(),
                      "native touchability can be restored");
          window->SetFocusable(true);
          self->Check(window->IsFocusable(), "native focusability can be restored");
          window->ShowInactive();
          self->window_phase = 1;
        });
    Check(submitted, "child creation submitted without blocking UI");
    if (!submitted)
      Finish();
  }
  void WindowTick() {
    ++phase_ticks;
    if (window_phase == 0)
      return;
    auto& manager = WindowManager::GetInstance();
    if (desktop && phase_ticks % 10 == 0) {
      const auto state = ohos::FindWindowState(main_window->GetNativeObject());
      const auto bounds = main_window->GetBounds();
      const auto minimum = main_window->GetMinimumSize(), maximum = main_window->GetMaximumSize();
      OH_LOG_Print(LOG_APP, LOG_INFO, 0x0001, "nativeapi-test",
                   "desktop phase %{public}d status %{public}d frame %{public}.1f,%{public}.1f "
                   "%{public}.1fx%{public}.1f layout %{public}d full %{public}d "
                   "limits %{public}.1fx%{public}.1f..%{public}.1fx%{public}.1f",
                   window_phase, ohos::WindowStatus(state), bounds.x, bounds.y, bounds.width,
                   bounds.height, main_window->IsContentUnderTitleBar(),
                   main_window->IsFullScreen(), minimum.width, minimum.height, maximum.width,
                   maximum.height);
    }
    auto close_to = [](double a, double b) { return std::abs(a - b) < 0.6; };
    if (window_phase == 1) {
      const auto bounds = child->GetBounds();
      if (!child->IsVisible() || !close_to(bounds.x, child_target.x) ||
          !close_to(bounds.y, child_target.y) || !close_to(bounds.width, child_target.width) ||
          !close_to(bounds.height, child_target.height))
        return;
      const auto current_content = child->GetContentBounds();
      if (phase_ticks % 10 == 1) {
        OH_LOG_Print(
            LOG_APP, LOG_INFO, 0x0001, "nativeapi-test",
            "window readiness: frame %{public}.1fx%{public}.1f content %{public}.1fx%{public}.1f "
            "created %{public}d resized %{public}d moved %{public}d object-resized %{public}d",
            bounds.width, bounds.height, current_content.width, current_content.height,
            manager_events["WindowCreatedEvent"], manager_events["WindowResizedEvent"],
            manager_events["WindowMovedEvent"], object_events["WindowResizedEvent"]);
      }
      if (current_content.width <= 0 || current_content.height <= 0 ||
          current_content.width > bounds.width || current_content.height > bounds.height ||
          manager_events["WindowCreatedEvent"] != 1 || manager_events["WindowResizedEvent"] == 0 ||
          manager_events["WindowMovedEvent"] == 0 || object_events["WindowResizedEvent"] == 0)
        return;
      Check(true, "child moved and resized in logical coordinates");
      auto state = ohos::FindWindowState(child->GetNativeObject());
      WindowManager_WindowProperties native{};
      OH_WindowManager_GetWindowProperties(state->system_id, &native);
      const auto scale = ohos::WindowScale(state);
      Check(close_to(bounds.width * scale, native.windowRect.width) &&
                close_to(bounds.x * scale, native.windowRect.posX),
            "window geometry matches public NDK pixels");
      const auto content = child->GetContentBounds();
      Check(content.width > 0 && content.height > 0 && content.width <= bounds.width &&
                content.height <= bounds.height,
            "drawable content bounds come from the native window");
      bool shown = false;
      OH_WindowManager_IsWindowShown(state->system_id, &shown);
      Check(shown && !child->IsFocused() && manager.GetCurrent() == main_window,
            "ShowInactive displays child without stealing focus");
      Check(manager.GetWindowAtPoint(overlap, IdAllocator::kInvalidId) == child &&
                manager.GetWindowAtPoint(overlap, child->GetId()) == main_window,
            "hit testing observes child order and exclusion");
      Check(manager_events["WindowCreatedEvent"] == 1 && manager_events["WindowResizedEvent"] > 0 &&
                manager_events["WindowMovedEvent"] > 0 && object_events["WindowResizedEvent"] > 0,
            "manager and object receive native creation and geometry events");
      const auto before = child->GetBounds();
      child->SetSize({std::numeric_limits<double>::quiet_NaN(), -1}, false);
      child->SetPosition({std::numeric_limits<double>::infinity(), 0});
      Check(close_to(child->GetBounds().width, before.width),
            "invalid geometry preserves native bounds");
      child->Focus();
      window_phase = 2;
    } else if (window_phase == 2 && child->IsFocused()) {
      Check(manager.GetCurrent() == child && manager_events["WindowFocusedEvent"] > 0,
            "Focus and current window follow native focus events");
      child->Blur();
      window_phase = 3;
    } else if (window_phase == 3 && main_window->IsFocused()) {
      Check(child->IsVisible() && !child->IsFocused(),
            "Blur returns focus to the previous window without hiding");
      child->Hide();
      window_phase = 4;
    } else if (window_phase == 4 && !child->IsVisible()) {
      Check(manager.GetWindowAtPoint(overlap, IdAllocator::kInvalidId) == main_window,
            "hidden child is excluded from hit testing");
      child->Show();
      window_phase = 5;
    } else if (window_phase == 5 && child->IsVisible() && child->IsFocused()) {
      Check(true, "Show restores child visibility and focus");
      close_listener = child->AddListener<WindowCloseRequestedEvent>(
          [weak = weak_from_this()](const auto& event) {
            if (auto self = weak.lock()) {
              ++self->close_requests;
              event.GetRequest()->Cancel();
            }
          });
      Check(child->Close(), "cancellable close request submitted");
      window_phase = 6;
      phase_ticks = 0;
    } else if (window_phase == 6 && phase_ticks >= 3) {
      Check(child->GetNativeObject() && child->IsVisible() && close_requests == 1,
            "cancelled close leaves the native child alive");
      child->RemoveListener(close_listener);
      close_listener = child->AddListener<WindowCloseRequestedEvent>(
          [weak = weak_from_this()](const auto& event) {
            if (auto self = weak.lock()) {
              ++self->close_requests;
              self->close_decision = event.GetRequest()->Defer();
            }
          });
      Check(child->Close() && child->Close(), "repeated close requests share the pending decision");
      window_phase = 7;
      phase_ticks = 0;
    } else if (window_phase == 7 && phase_ticks >= 3 && close_decision) {
      Check(close_requests == 2 && child->GetNativeObject() && close_decision->IsPending(),
            "deferred close does not destroy the native child prematurely");
      auto decision = close_decision;
      std::thread([decision] { decision->Accept(); }).detach();
      window_phase = 8;
    } else if (window_phase == 8 && !child->GetNativeObject()) {
      Check(!duplicate->GetNativeObject() && !manager.Get(child->GetId()) &&
                manager.GetAll().size() == 1,
            "native destruction invalidates all wrappers and registry lookups");
      Check(!child->Close() && child->GetBounds().width == 0 && !child->IsVisible(),
            "retained closed wrappers reject later operations");
      Check(manager_events["WindowClosedEvent"] == 1 && object_events["WindowClosedEvent"] == 1,
            "native close event is delivered exactly once");
      child->RemoveAllListeners();
      duplicate->RemoveAllListeners();
      manager.RemoveListener(manager_listener);
      manager_listener = 0;
      Check(manager.GetTotalListenerCount() == 0, "last window observer stops manager monitoring");
      manager_listener =
          manager.AddListener<WindowEvent>([weak = weak_from_this()](const WindowEvent& event) {
            if (auto self = weak.lock()) {
              self->event_threads_ok &= IsMainThread();
              ++self->manager_events[event.GetTypeName()];
            }
          });
      const bool submitted = ohos::CreateSubWindow(
          "NativeAPI_host_owned", "pages/Child", [self = shared_from_this()](auto window) {
            self->Check(window != nullptr, "window monitor can restart for a new child");
            if (!window) {
              self->Finish();
              return;
            }
            self->child = window;
            auto state = ohos::FindWindowState(window->GetNativeObject());
            ohos::CallWindow(
                state, "destroyWindow");  // Host destruction, without nativeapi::Window::Close.
            self->window_phase = 9;
          });
      Check(submitted, "second child creation submitted");
      window_phase = 0;
    } else if (window_phase == 9 && !child->GetNativeObject()) {
      Check(manager.GetAll().size() == 1 && manager_events["WindowClosedEvent"] == 2,
            "host-side destruction also invalidates nativeapi identities");
      Check(event_threads_ok, "all window event callbacks run on ArkTS UI thread");
      Check(ohos::CreateSubWindow("NativeAPI_missing_content", "pages/DoesNotExist",
                                  [self = shared_from_this()](auto window) {
                                    ++self->failed_creations;
                                    self->Check(!window,
                                                "failed child content loading reports failure");
                                    self->window_phase = 12;
                                  }),
            "failed-content creation submitted asynchronously");
      window_phase = 0;
    } else if (window_phase == 12 && manager.GetAll().size() == 1) {
      Check(failed_creations == 1,
            "failed content creation completes once and destroys the hidden child");
      abort_creation_listener =
          manager.AddListener<WindowCreatedEvent>([weak = weak_from_this()](const auto& event) {
            if (auto self = weak.lock()) {
              if (auto window = WindowManager::GetInstance().Get(event.GetWindowId()))
                window->Close();
              WindowManager::GetInstance().RemoveListener(self->abort_creation_listener);
              self->abort_creation_listener = 0;
            }
          });
      Check(ohos::CreateSubWindow(
                "NativeAPI_reentrant_close", "pages/Child",
                [self = shared_from_this()](auto window) {
                  ++self->failed_creations;
                  self->Check(
                      !window,
                      "creation callback handles reentrant destruction from a created listener");
                  self->window_phase = 13;
                }),
            "reentrant-close creation submitted");
      window_phase = 0;
    } else if (window_phase == 13 && manager.GetAll().size() == 1) {
      Check(failed_creations == 2,
            "reentrant-close factory completion runs exactly once without leaking a child");
      if (desktop) {
        // A floating desktop main window cannot have full-screen content layout.
        entered_before = manager_events["WindowEnteredFullScreenEvent"];
        main_window->SetFullScreen(true);
        window_phase = 30;
      } else {
        main_window->SetContentUnderTitleBar(true);
        window_phase = 10;
      }
    } else if (window_phase == 30 && main_window->IsFullScreen()) {
      const auto bounds = main_window->GetBounds();
      const auto display = DisplayManager::GetInstance().GetPrimary()->GetSize();
      if (!close_to(bounds.width, display.width) || !close_to(bounds.height, display.height))
        return;
      Check(manager_events["WindowEnteredFullScreenEvent"] == entered_before + 1,
            "desktop full screen covers the real display and emits one native event");
      main_window->SetContentUnderTitleBar(true);
      window_phase = 10;
    } else if (window_phase == 10 && main_window->IsContentUnderTitleBar()) {
      Check(true, "immersive content layout reaches the native main window");
      main_window->SetContentUnderTitleBar(false);
      window_phase = 11;
    } else if (window_phase == 11 && !main_window->IsContentUnderTitleBar()) {
      Check(true, "native content layout can be restored");
      if (desktop) {
        exited_before = manager_events["WindowExitedFullScreenEvent"];
        main_window->SetFullScreen(false);
        window_phase = 15;
      } else {
        entered_before = manager_events["WindowEnteredFullScreenEvent"];
        main_window->SetFullScreen(true);
        window_phase = 14;
      }
    } else if (window_phase == 14 && main_window->IsFullScreen()) {
      Check(manager_events["WindowEnteredFullScreenEvent"] == entered_before + 1,
            "full-screen getter and entered event agree with native system-bar state");
      exited_before = manager_events["WindowExitedFullScreenEvent"];
      main_window->SetFullScreen(false);
      window_phase = 15;
    } else if (window_phase == 15 && !main_window->IsFullScreen()) {
      Check(manager_events["WindowExitedFullScreenEvent"] == exited_before + 1,
            "native full-screen exit restores system bars and emits one event");
      if (desktop) {
        main_window->SetTitle("NativeAPI Desktop Acceptance");
        main_window->SetTitleBarStyle(TitleBarStyle::Hidden);
        main_window->SetResizable(false);
        main_window->SetMovable(false);
        window_phase = 20;
      } else {
        BeginDialogs();
      }
    } else if (window_phase == 20 && main_window->GetTitle() == "NativeAPI Desktop Acceptance" &&
               main_window->GetTitleBarStyle() == TitleBarStyle::Hidden) {
      Check(true, "desktop title updates and native decorations can be hidden");
      Check(!main_window->IsResizable() && !main_window->IsMovable(),
            "desktop drag policies acknowledge disabling");
      main_window->SetTitleBarStyle(TitleBarStyle::Normal);
      main_window->SetResizable(true);
      main_window->SetMovable(true);
      main_window->SetMinimumSize({600, 400});
      main_window->SetMaximumSize({1200, 900});
      window_phase = 21;
    } else if (window_phase == 21 && main_window->GetTitleBarStyle() == TitleBarStyle::Normal &&
               main_window->IsResizable() && main_window->IsMovable()) {
      const auto minimum = main_window->GetMinimumSize(), maximum = main_window->GetMaximumSize();
      if (!close_to(minimum.width, 600) || !close_to(minimum.height, 400) ||
          !close_to(maximum.width, 1200) || !close_to(maximum.height, 900))
        return;
      Check(true, "desktop decorations and drag policies can be restored");
      Check(true, "desktop minimum and maximum sizes round trip through native limits");
      main_window->SetBounds({120, 120, 640, 480});
      window_phase = 22;
    } else if (window_phase == 22) {
      const auto bounds = main_window->GetBounds();
      if (!close_to(bounds.x, 120) || !close_to(bounds.y, 120) || !close_to(bounds.width, 640) ||
          !close_to(bounds.height, 480))
        return;
      Check(true, "desktop main window actually moves and resizes");
      maximized_before = manager_events["WindowMaximizedEvent"];
      main_window->Maximize();
      window_phase = 23;
    } else if (window_phase == 23 && main_window->IsMaximized()) {
      if (manager_events["WindowMaximizedEvent"] <= maximized_before)
        return;
      Check(!main_window->IsFullScreen() && main_window->GetBounds().width > 640,
            "desktop maximize expands native geometry without entering full screen");
      Check(true, "desktop maximize delivers native status event");
      entered_before = manager_events["WindowEnteredFullScreenEvent"];
      main_window->SetFullScreen(true);
      window_phase = 27;
    } else if (window_phase == 27 && main_window->IsFullScreen()) {
      Check(manager_events["WindowEnteredFullScreenEvent"] == entered_before + 1 &&
                !main_window->IsMaximized(),
            "desktop maximized window can enter distinct full-screen mode");
      exited_before = manager_events["WindowExitedFullScreenEvent"];
      main_window->SetFullScreen(false);
      window_phase = 28;
    } else if (window_phase == 28 && main_window->IsMaximized() && !main_window->IsFullScreen()) {
      Check(manager_events["WindowExitedFullScreenEvent"] == exited_before + 1,
            "desktop full-screen exit preserves the previous maximized state");
      restored_before = manager_events["WindowRestoredEvent"];
      main_window->Unmaximize();
      window_phase = 24;
    } else if (window_phase == 24 && !main_window->IsMaximized()) {
      const auto bounds = main_window->GetBounds();
      if (ohos::WindowStatus(ohos::FindWindowState(main_window->GetNativeObject())) != 4 ||
          !close_to(bounds.width, 640) || !close_to(bounds.height, 480))
        return;
      Check(manager_events["WindowRestoredEvent"] > restored_before,
            "desktop unmaximize restores floating geometry and native event");
      minimized_before = manager_events["WindowMinimizedEvent"];
      main_window->Minimize();
      window_phase = 25;
    } else if (window_phase == 25 && main_window->IsMinimized() && !main_window->IsVisible()) {
      Check(manager_events["WindowMinimizedEvent"] > minimized_before,
            "desktop minimize hides main window and delivers native event");
      main_window->Restore();
      window_phase = 26;
    } else if (window_phase == 26 && main_window->IsVisible() && !main_window->IsMinimized() &&
               main_window->IsFocused()) {
      Check(true, "desktop restore returns minimized main window to foreground");
      BeginDialogs();
    }
  }
  void DialogReady(const char* phase) {
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0001, "nativeapi-test",
                 "NATIVEAPI_DIALOG_UI_READY %{public}s", phase);
  }
  void BeginDialogs() {
    stage = 4;
    ticks = 0;
    Check(MessageDialog::IsExtendedSupported(), "native ArkUI extended dialogs available");
    dialog = std::make_shared<MessageDialog>("NativeAPI Dialog Primary", "Native dialog 中文🙂");
    Check(dialog->GetTitle() == "NativeAPI Dialog Primary" &&
              dialog->GetMessage() == "Native dialog 中文🙂",
          "dialog initial title and message");
    Check(!dialog->SetButtons("", "", "") && !dialog->SetInputText(std::string("bad\0value", 9)) &&
              !dialog->SetProgress(std::numeric_limits<double>::quiet_NaN()) &&
              !dialog->SetProgress(1.1) &&
              !dialog->SetDefaultButton(static_cast<MessageDialogResult>(99)),
          "dialog rejects invalid labels, strings, progress and button enums");
    Check(dialog->SetButtons("Accept NativeAPI", "Other NativeAPI", "Dismiss NativeAPI") &&
              dialog->SetDefaultButton(MessageDialogResult::Primary) &&
              dialog->SetParentWindow(main_window) && dialog->SetInputEnabled(true) &&
              dialog->SetInputText("预填 NativeAPI 🙂") &&
              dialog->SetCheckbox("NativeAPI remember", false) && dialog->SetProgress(0.25),
          "configure native dialog controls");
    Check(dialog->Open() && dialog->IsOpen(), "nonmodal dialog opens through actual UI context");
    if (!dialog->IsOpen()) {
      Finish();
      return;
    }
    Check(!dialog->Open() && !dialog->SetButtons("Changed", "", "") &&
              !dialog->SetParentWindow(nullptr),
          "open dialog rejects duplicate opening and structural changes");
    dialog_phase = 1;
    DialogReady("primary");
  }
  void DialogTick() {
    ++dialog_ticks;
    if (dialog_phase == 1 && !dialog->IsOpen()) {
      Check(dialog->GetResult() == MessageDialogResult::Primary && dialog->IsCheckboxChecked() &&
                dialog->GetInputText() == "预填 NativeAPI 🙂",
            "native primary click preserves input and actual checkbox change");
      dialog_phase = 2;
      dialog_ticks = 0;
    } else if (dialog_phase == 2 && dialog_ticks >= 5) {
      dialog->SetTitle("NativeAPI Dialog Secondary");
      Check(dialog->SetProgress(-1) && dialog->Open(), "reuse dialog with indeterminate progress");
      dialog_phase = 3;
      DialogReady("secondary");
    } else if (dialog_phase == 3 && !dialog->IsOpen()) {
      Check(dialog->GetResult() == MessageDialogResult::Secondary,
            "native secondary button result");
      dialog_phase = 4;
      dialog_ticks = 0;
    } else if (dialog_phase == 4 && dialog_ticks >= 5) {
      dialog->SetTitle("NativeAPI Dialog Close");
      Check(dialog->SetInputEnabled(false) && dialog->SetCheckbox("", false) &&
                dialog->SetProgress(-2) && dialog->Open(),
            "reopen dialog with optional controls hidden");
      dialog_phase = 5;
      DialogReady("close");
    } else if (dialog_phase == 5 && !dialog->IsOpen()) {
      Check(dialog->GetResult() == MessageDialogResult::Close, "native close button result");
      dialog_phase = 6;
      dialog_ticks = 0;
    } else if (dialog_phase == 6 && dialog_ticks >= 5) {
      dialog->SetTitle("NativeAPI Dialog Programmatic");
      Check(dialog->Open(), "dialog opens for live updates");
      dialog->SetTitle("NativeAPI Dialog Updated");
      dialog->SetMessage("Live native update");
      Check(dialog->SetInputEnabled(true) && dialog->SetInputText("updated") &&
                dialog->SetCheckbox("Updated checkbox", true) && dialog->SetProgress(0.8) &&
                dialog->GetTitle() == "NativeAPI Dialog Updated" &&
                dialog->GetMessage() == "Live native update" &&
                dialog->GetInputText() == "updated" && dialog->IsCheckboxChecked(),
            "live updates reach native controls");
      dialog_phase = 7;
      dialog_ticks = 0;
    } else if (dialog_phase == 7 && dialog_ticks >= 8) {
      Check(dialog->Close() && !dialog->Close(), "programmatic close accepted once");
      dialog_phase = 8;
    } else if (dialog_phase == 8 && !dialog->IsOpen()) {
      Check(dialog->GetResult() == MessageDialogResult::Close,
            "programmatic close finishes native animation");
      dialog_phase = 9;
      dialog_ticks = 0;
    } else if (dialog_phase == 9 && dialog_ticks >= 5) {
      dialog->SetModality(DialogModality::Window);
      Check(!dialog->Open(), "blocking modal open rejects the ArkTS UI thread");
      dialog->SetTitle("NativeAPI Dialog Modal");
      auto self = shared_from_this();
      std::thread([self, held = dialog] {
        self->modal_ok = held->Open();
        self->modal_done = true;
      }).detach();
      dialog_phase = 10;
    } else if (dialog_phase == 10 && dialog->IsOpen()) {
      Check(!modal_done, "worker modal open waits while the UI loop remains responsive");
      dialog_phase = 11;
      DialogReady("modal");
    } else if (dialog_phase == 11 && modal_done) {
      Check(modal_ok && !dialog->IsOpen() && dialog->GetResult() == MessageDialogResult::Primary,
            "worker modal returns after native dismissal");
      dialog.reset();
      dialog =
          std::make_shared<MessageDialog>("NativeAPI Dialog Destructor", "Dispose while visible");
      Check(dialog->SetParentWindow(main_window) && dialog->Open(),
            "open dialog for destruction cleanup");
      dialog_phase = 12;
      dialog_ticks = 0;
    } else if (dialog_phase == 12 && dialog_ticks >= 5) {
      dialog.reset();
      dialog_phase = 13;
      dialog_ticks = 0;
    } else if (dialog_phase == 13 && dialog_ticks >= 8) {
      Check(ohos::CreateSubWindow("NativeAPI_dialog_parent", "pages/Child",
                                  [self = shared_from_this()](auto window) {
                                    self->Check(window != nullptr,
                                                "create native parent for dialog lifecycle test");
                                    if (!window) {
                                      self->Finish();
                                      return;
                                    }
                                    self->child = window;
                                    window->SetBounds({80, 100, 400, 450});
                                    window->Show();
                                    self->dialog_phase = 14;
                                  }),
            "submit dialog parent creation");
      dialog_phase = 0;
    } else if (dialog_phase == 14 && child->IsVisible()) {
      dialog = std::make_shared<MessageDialog>("NativeAPI Dialog Parent",
                                               "Parent closure dismisses child dialog");
      Check(dialog->SetParentWindow(child) && dialog->Open(),
            "dialog binds to a real child UI context");
      dialog_phase = 15;
      dialog_ticks = 0;
    } else if (dialog_phase == 15 && dialog_ticks >= 8) {
      Check(child->Close(), "close dialog parent through native window lifecycle");
      dialog_phase = 16;
    } else if (dialog_phase == 16 && !child->GetNativeObject()) {
      Check(!dialog->IsOpen() && dialog->GetResult() == MessageDialogResult::Close,
            "parent destruction dismisses dialog and invalidates native controls");
      dialog.reset();
      main_window->Focus();
      Check(ohos::CreateSubWindow("NativeAPI_dialog_other", "pages/Child",
                                  [self = shared_from_this()](auto window) {
                                    self->Check(window != nullptr,
                                                "create second window for application modality");
                                    if (!window) {
                                      self->Finish();
                                      return;
                                    }
                                    self->dialog_other = window;
                                    window->ShowInactive();
                                    self->dialog_phase = 17;
                                  }),
            "submit application modal sibling creation");
      dialog_phase = 0;
    } else if (dialog_phase == 17 && dialog_other->IsVisible()) {
      dialog = std::make_shared<MessageDialog>("NativeAPI Dialog Application",
                                               "All attached windows are blocked");
      dialog->SetParentWindow(main_window);
      dialog->SetModality(DialogModality::Application);
      dialog_snapshot_listener = WindowManager::GetInstance().AddListener<WindowCreatedEvent>(
          [self = shared_from_this()](const auto& event) {
            auto window = WindowManager::GetInstance().Get(event.GetWindowId());
            if (window && window != self->main_window && window != self->dialog_other) {
              self->late_snapshot = true;
              self->late_focusable = window->IsFocusable();
              self->late_ignore_mouse = window->IsIgnoreMouseEvents();
            }
          });
      modal_done = modal_ok = false;
      auto self = shared_from_this();
      std::thread([self, held = dialog] {
        self->modal_ok = held->Open();
        self->modal_done = true;
      }).detach();
      dialog_phase = 18;
    } else if (dialog_phase == 18 && dialog->IsOpen()) {
      Check(!dialog_other->IsFocusable() && dialog_other->IsIgnoreMouseEvents() && !modal_done,
            "application modal disables native input on sibling window");
      Check(ohos::CreateSubWindow("NativeAPI_dialog_late", "pages/Child",
                                  [self = shared_from_this()](auto window) {
                                    self->Check(window != nullptr,
                                                "create window during application modal");
                                    if (!window) {
                                      self->Finish();
                                      return;
                                    }
                                    self->child = window;
                                    window->ShowInactive();
                                    self->dialog_phase = 19;
                                  }),
            "submit late modal sibling creation");
      dialog_phase = 0;
    } else if (dialog_phase == 19 && child->IsVisible()) {
      Check(!child->IsFocusable() && child->IsIgnoreMouseEvents(),
            "application modal also blocks newly attached windows");
      nested_dialog = std::make_shared<MessageDialog>("NativeAPI Dialog Nested",
                                                      "Nested application modal lease");
      nested_dialog->SetParentWindow(main_window);
      nested_dialog->SetModality(DialogModality::Application);
      auto self = shared_from_this();
      std::thread([self, held = nested_dialog] {
        self->nested_ok = held->Open();
        self->nested_done = true;
      }).detach();
      dialog_phase = 23;
    } else if (dialog_phase == 23 && nested_dialog->IsOpen()) {
      Check(!nested_done && !child->IsFocusable() && child->IsIgnoreMouseEvents(),
            "overlapping application dialogs retain independent native input leases");
      Check(dialog->Close(), "application modal can be closed programmatically");
      dialog_phase = 24;
    } else if (dialog_phase == 24 && modal_done) {
      Check(nested_dialog->IsOpen() && !dialog_other->IsFocusable() &&
                dialog_other->IsIgnoreMouseEvents(),
            "closing first application dialog keeps siblings blocked by the second");
      Check(nested_dialog->Close(), "close remaining application modal lease");
      dialog_phase = 20;
    } else if (dialog_phase == 20 && modal_done && nested_done) {
      Check(nested_ok && !nested_dialog->IsOpen(),
            "nested application modal returns after native dismissal");
      nested_dialog.reset();
      Check(modal_ok && !dialog->IsOpen(), "application modal returns after native dismissal");
      Check(dialog_other->IsFocusable() && !dialog_other->IsIgnoreMouseEvents(),
            "application modal restores existing sibling input policies");
      Check(late_snapshot && child->IsFocusable() == late_focusable &&
                child->IsIgnoreMouseEvents() == late_ignore_mouse,
            "application modal restores newly attached sibling input policies");
      WindowManager::GetInstance().RemoveListener(dialog_snapshot_listener);
      dialog_snapshot_listener = 0;
      dialog.reset();
      Check(child->Close() && dialog_other->Close(), "close modality test windows");
      dialog_phase = 21;
    } else if (dialog_phase == 21 && !child->GetNativeObject() &&
               !dialog_other->GetNativeObject()) {
      main_window->Focus();
      dialog_phase = 22;
    } else if (dialog_phase == 22 && main_window->IsFocused()) {
      c_dialog = native_message_dialog_create("NativeAPI Dialog C ABI", "Generated native calls");
      auto parent_handle = native_window_manager_get_current();
      Check(c_dialog && native_message_dialog_is_extended_supported() &&
                native_message_dialog_set_buttons(c_dialog, "Confirm", "Other", "Close") &&
                native_message_dialog_set_default_button(c_dialog,
                                                         NATIVE_MESSAGE_DIALOG_RESULT_PRIMARY) &&
                native_message_dialog_set_parent_window(c_dialog, parent_handle) &&
                native_message_dialog_set_input_enabled(c_dialog, true) &&
                native_message_dialog_set_input_text(c_dialog, "ffi 中文🙂") &&
                native_message_dialog_set_checkbox(c_dialog, "C ABI checkbox", true) &&
                native_message_dialog_set_progress(c_dialog, 0.5),
            "generated C ABI configures native dialog controls");
      native_window_free(parent_handle);
      native_message_dialog_set_title(c_dialog, "NativeAPI Dialog C ABI updated");
      native_message_dialog_set_message(c_dialog, "Updated through generated ABI");
      native_message_dialog_set_modality(c_dialog, NATIVE_DIALOG_MODALITY_NONE);
      char* title = native_message_dialog_get_title(c_dialog);
      char* message = native_message_dialog_get_message(c_dialog);
      char* input = native_message_dialog_get_input_text(c_dialog);
      Check(title && std::string(title) == "NativeAPI Dialog C ABI updated" && message &&
                std::string(message) == "Updated through generated ABI" && input &&
                std::string(input) == "ffi 中文🙂" &&
                native_message_dialog_is_checkbox_checked(c_dialog) &&
                native_message_dialog_get_modality(c_dialog) == NATIVE_DIALOG_MODALITY_NONE,
            "generated C ABI returns owned strings and dialog state");
      free_c_str(title);
      free_c_str(message);
      free_c_str(input);
      Check(native_message_dialog_open(c_dialog) && native_message_dialog_is_open(c_dialog),
            "generated C ABI opens actual native dialog");
      dialog_phase = 25;
      dialog_ticks = 0;
    } else if (dialog_phase == 25 && dialog_ticks >= 8) {
      Check(native_message_dialog_close(c_dialog), "generated C ABI requests native dismissal");
      dialog_phase = 26;
    } else if (dialog_phase == 26 && !native_message_dialog_is_open(c_dialog)) {
      Check(native_message_dialog_get_result(c_dialog) == NATIVE_MESSAGE_DIALOG_RESULT_CLOSE,
            "generated C ABI observes completed native dismissal");
      native_message_dialog_free(c_dialog);
      native_message_dialog_free(c_dialog);
      Check(!native_message_dialog_is_open(c_dialog) && !native_message_dialog_open(c_dialog),
            "released generated dialog handles fail safely");
      c_dialog = NATIVE_INVALID_MESSAGE_DIALOG;
      Finish();
    }
  }
  void Finish() {
    if (done)
      return;
    done = true;
    if (c_dialog) {
      native_message_dialog_free(c_dialog);
      c_dialog = NATIVE_INVALID_MESSAGE_DIALOG;
    }
    if (child)
      child->RemoveAllListeners();
    if (duplicate)
      duplicate->RemoveAllListeners();
    if (dialog_snapshot_listener)
      WindowManager::GetInstance().RemoveListener(dialog_snapshot_listener);
    if (manager_listener)
      WindowManager::GetInstance().RemoveListener(manager_listener);
    if (abort_creation_listener)
      WindowManager::GetInstance().RemoveListener(abort_creation_listener);
    if (listener)
      Clipboard::GetInstance().RemoveListener(listener);
    const auto marker = failed == 0 ? "NATIVEAPI_OHOS_PASS" : "NATIVEAPI_OHOS_FAIL";
    std::ostringstream summary;
    summary << marker << " (" << passed << " passed, " << failed << " failed)\n";
    report << summary.str();
    // HiLog truncates long records. Checks are already logged individually;
    // keep the runner's final marker in a short, dedicated record.
    OH_LOG_Print(LOG_APP, failed ? LOG_ERROR : LOG_INFO, 0x0001, "nativeapi-test", "%{public}s",
                 summary.str().c_str());
    napi_handle_scope scope;
    napi_open_handle_scope(env, &scope);
    napi_value result;
    auto text = report.str();
    napi_create_string_utf8(env, text.c_str(), text.size(), &result);
    napi_resolve_deferred(env, deferred, result);
    napi_close_handle_scope(env, scope);
    uv_timer_stop(&timer);
    uv_close(reinterpret_cast<uv_handle_t*>(&timer), [](uv_handle_t* handle) {
      delete static_cast<std::shared_ptr<Tests>*>(handle->data);
    });
  }
};

napi_value Run(napi_env env, napi_callback_info) {
  auto tests = std::make_shared<Tests>();
  tests->env = env;
  napi_value promise;
  napi_create_promise(env, &tests->deferred, &promise);
  uv_loop_t* loop = nullptr;
  napi_get_uv_event_loop(env, &loop);
  uv_timer_init(loop, &tests->timer);
  tests->timer.data = new std::shared_ptr<Tests>(tests);
  uv_timer_start(
      &tests->timer,
      [](uv_timer_t* timer) { (*static_cast<std::shared_ptr<Tests>*>(timer->data))->Tick(); }, 100,
      100);
  tests->Begin();
  return promise;
}
napi_value AttachStage(napi_env env, napi_callback_info info) {
  napi_value value;
  size_t count = 1;
  napi_get_cb_info(env, info, &count, &value, nullptr, nullptr);
  napi_value result;
  napi_get_boolean(env, count == 1 && ohos::AttachWindowStage(env, value), &result);
  return result;
}
napi_value AttachContext(napi_env env, napi_callback_info info) {
  napi_value context;
  size_t count = 1;
  napi_get_cb_info(env, info, &count, &context, nullptr, nullptr);
  napi_value result;
  const bool attached = count == 1 && ohos::AttachContext(env, context);
  if (attached)
    nativeapi::tests::app_journal.attached_metadata = Application::GetInstance().IsSingleInstance() &&
        !Application::GetInstance().IsRunning();
  napi_get_boolean(env, attached, &result);
  return result;
}

napi_value Init(napi_env env, napi_value exports) {
  int callbacks = 0;
  bool rejected = false;
  Clipboard::GetInstance().ReadText([&](bool ok, auto text) {
    ++callbacks;
    rejected = !ok && !text;
  });
  uninitialized_closed = !Clipboard::IsSupported() &&
                         !Clipboard::GetInstance().WriteText("not initialized") && callbacks == 1 &&
                         rejected;
  nativeapi::tests::url_uninitialized_closed = !UrlOpener::GetInstance().IsSupported() &&
                                              !UrlOpener::GetInstance().CanOpen("https://example.com");
  auto& accessibility = AccessibilityManager::GetInstance();
  bool accessibility_before = !accessibility.IsEnabled() && !native_accessibility_manager_is_enabled();
  accessibility.Enable();
  native_accessibility_manager_enable();
  nativeapi::tests::accessibility_uninitialized_closed = accessibility_before &&
      !accessibility.IsEnabled() && !native_accessibility_manager_is_enabled();
  nativeapi::tests::InitializeStartupTest();
  nativeapi::tests::InitializeDeviceInfoTest(env);
  nativeapi::tests::InitializeKeyboardTest();
  ohos::Initialize(env);
  nativeapi::tests::InstallDisplayTestDispatcher();
  nativeapi::tests::InstallApplicationJournal();
  napi_property_descriptor device_info[] = {
    {"attachShadowHost", nullptr, nativeapi::tests::AttachShadowHost, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"shadowTargetAttach", nullptr, nativeapi::tests::ShadowTargetAttach, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"shadowState", nullptr, nativeapi::tests::ShadowState, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"shadowAction", nullptr, nativeapi::tests::ShadowAction, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"shadowProbeImage", nullptr, nativeapi::tests::ShadowProbeImage, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"foundationValues", nullptr, nativeapi::tests::FoundationValues, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"foundationWindow", nullptr, nativeapi::tests::FoundationWindow, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"preferencesCommand", nullptr, nativeapi::tests::PreferencesCommand, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"deviceInfoCheck", nullptr, nativeapi::tests::DeviceInfoCheck, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, sizeof(device_info) / sizeof(device_info[0]), device_info);
  napi_property_descriptor manager_tests[] = {
    {"tooltipAttach", nullptr, nativeapi::tests::TooltipAttach, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"tooltipCommand", nullptr, nativeapi::tests::TooltipCommand, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"parentState", nullptr, nativeapi::tests::ParentState, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"parentCommand", nullptr, nativeapi::tests::ParentCommand, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"managerState", nullptr, nativeapi::tests::ManagerState, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"managerQuery", nullptr, nativeapi::tests::ManagerQuery, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, sizeof(manager_tests) / sizeof(manager_tests[0]), manager_tests);
  napi_property_descriptor keyboard[] = {
    {"keyboardState", nullptr, nativeapi::tests::KeyboardState, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, 1, keyboard);
  napi_property_descriptor startup[] = {
    {"startupCheck", nullptr, nativeapi::tests::StartupCheck, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, 1, startup);
  napi_property_descriptor source[] = {
    {"nativeCloseState", nullptr, nativeapi::tests::NativeCloseState, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"nativeCloseAttach", nullptr, nativeapi::tests::NativeCloseAttach, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"sourceState", nullptr, nativeapi::tests::SourceState, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"sourceBegin", nullptr, nativeapi::tests::SourceBegin, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, sizeof(source) / sizeof(source[0]), source);
  napi_property_descriptor drops[] = {
    {"dropState", nullptr, nativeapi::tests::DropState, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, 1, drops);
  napi_property_descriptor shortcuts[] = {
    {"shortcutHostAttach", nullptr, nativeapi::tests::ShortcutHostAttach, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"shortcutHostDetach", nullptr, nativeapi::tests::ShortcutHostDetach, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"shortcutKey", nullptr, nativeapi::tests::ShortcutKey, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"shortcutState", nullptr, nativeapi::tests::ShortcutState, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, sizeof(shortcuts) / sizeof(shortcuts[0]), shortcuts);
  napi_property_descriptor clipboard_interop[] = {
    {"clipboardInterop", nullptr, nativeapi::tests::ClipboardInterop, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, 1, clipboard_interop);
  napi_property_descriptor display[] = {
    {"displayState", nullptr, nativeapi::tests::DisplayState, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, 1, display);
  napi_property_descriptor cursor[] = {
    {"cursorProbe", nullptr, nativeapi::tests::CursorProbe, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, 1, cursor);
  napi_property_descriptor controls[] = {
    {"aspectState", nullptr, nativeapi::tests::AspectState, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"aspectAction", nullptr, nativeapi::tests::AspectAction, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"controlsState", nullptr, nativeapi::tests::ControlsState, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"controlsAction", nullptr, nativeapi::tests::ControlsAction, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, 4, controls);
  napi_property_descriptor title[] = {
    {"modeState", nullptr, nativeapi::tests::ModeState, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"modeSeed", nullptr, nativeapi::tests::ModeSeed, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"modeAction", nullptr, nativeapi::tests::ModeAction, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"layoutState", nullptr, nativeapi::tests::LayoutState, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"layoutAction", nullptr, nativeapi::tests::LayoutAction, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"fullscreenState", nullptr, nativeapi::tests::FullscreenState, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"fullscreenAction", nullptr, nativeapi::tests::FullscreenAction, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"dragSessionProbe", nullptr, nativeapi::tests::DragSessionProbe, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"titleState", nullptr, nativeapi::tests::TitleState, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"titleAction", nullptr, nativeapi::tests::TitleAction, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, sizeof(title) / sizeof(title[0]), title);
  napi_property_descriptor drags[] = {
    {"dragState", nullptr, nativeapi::tests::DragState, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"dragStart", nullptr, nativeapi::tests::DragStart, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, 2, drags);
  napi_property_descriptor shapes[] = {
    {"shapeState", nullptr, nativeapi::tests::ShapeState, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"shapeAction", nullptr, nativeapi::tests::ShapeAction, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, 2, shapes);
  napi_property_descriptor styles[] = {
    {"styleState", nullptr, nativeapi::tests::StyleState, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"styleAction", nullptr, nativeapi::tests::StyleAction, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, 2, styles);
  napi_property_descriptor occlusions[] = {
    {"occlusionCommand", nullptr, nativeapi::tests::OcclusionCommand, nullptr, nullptr, nullptr, napi_default, nullptr},
    {"occlusionCheck", nullptr, nativeapi::tests::OcclusionCheck, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, 2, occlusions);
  napi_property_descriptor integrations[] = {
    {"setupIntegrations",nullptr,nativeapi::tests::SetupIntegrations,nullptr,nullptr,nullptr,napi_default,nullptr},
    {"integrationState",nullptr,nativeapi::tests::IntegrationState,nullptr,nullptr,nullptr,napi_default,nullptr},
    {"integrationAction",nullptr,nativeapi::tests::IntegrationAction,nullptr,nullptr,nullptr,napi_default,nullptr}};
  napi_define_properties(env,exports,3,integrations);
  napi_property_descriptor descriptor{"run",   nullptr, Run,          nullptr,
                                      nullptr, nullptr, napi_default, nullptr};
  napi_define_properties(env, exports, 1, &descriptor);
  napi_property_descriptor pickers{"runPickers", nullptr, nativeapi::tests::RunPickers,
                                   nullptr,      nullptr, nullptr,
                                   napi_default, nullptr};
  napi_define_properties(env, exports, 1, &pickers);
  napi_property_descriptor tray{"runTray", nullptr, nativeapi::tests::RunTray, nullptr, nullptr, nullptr, napi_default, nullptr};
  napi_define_properties(env, exports, 1, &tray);
  napi_property_descriptor tray_worker{"runTrayWorker", nullptr, nativeapi::tests::RunTrayWorker, nullptr, nullptr, nullptr, napi_default, nullptr};
  napi_define_properties(env, exports, 1, &tray_worker);
  napi_property_descriptor urls{"runUrls", nullptr, nativeapi::tests::RunUrls, nullptr,
                                nullptr, nullptr, napi_default, nullptr};
  napi_define_properties(env, exports, 1, &urls);
  napi_property_descriptor notifications{"runNotifications", nullptr, nativeapi::tests::RunNotifications,
      nullptr, nullptr, nullptr, napi_default, nullptr};
  napi_define_properties(env, exports, 1, &notifications);
  napi_property_descriptor notification_want{"handleNotificationWant", nullptr, nativeapi::tests::HandleNotificationWant,
      nullptr, nullptr, nullptr, napi_default, nullptr};
  napi_define_properties(env, exports, 1, &notification_want);
  napi_property_descriptor attach{
      "attachWindowStage", nullptr, AttachStage, nullptr, nullptr, nullptr, napi_default, nullptr};
  napi_define_properties(env, exports, 1, &attach);
  napi_property_descriptor boundary{"clipboardBoundary", nullptr, nativeapi::tests::ClipboardBoundary, nullptr, nullptr, nullptr, napi_default, nullptr};
  napi_define_properties(env, exports, 1, &boundary);
  napi_property_descriptor context{"attachContext", nullptr, AttachContext, nullptr,
                                   nullptr,         nullptr, napi_default,  nullptr};
  napi_define_properties(env, exports, 1, &context);
  napi_property_descriptor views[] = {
      {"attachViewContent", nullptr, nativeapi::tests::AttachViews, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"detachViewContent", nullptr, nativeapi::tests::DetachViews, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"runViews", nullptr, nativeapi::tests::RunViews, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, 3, views);
  napi_property_descriptor menus[] = {
      {"attachMenuHost", nullptr, nativeapi::tests::AttachMenus, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"runMenus", nullptr, nativeapi::tests::RunMenus, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, 2, menus);
  napi_property_descriptor access{"runAccessibility", nullptr, nativeapi::tests::RunAccessibility,
      nullptr, nullptr, nullptr, napi_default, nullptr};
  napi_define_properties(env, exports, 1, &access);
  napi_property_descriptor application[] = {
      {"applicationLifecycle", nullptr, nativeapi::tests::ApplicationLifecycle, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"runApplication", nullptr, nativeapi::tests::RunApplication, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"setApplicationTheme", nullptr, nativeapi::tests::SetApplicationTheme, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"runApplicationQuit", nullptr, nativeapi::tests::RunApplicationQuit, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, 4, application);
  return exports;
}
static napi_module module = {1, 0, nullptr, Init, "nativeapi_smoke", nullptr, {0}};
__attribute__((constructor)) void Register() {
  napi_module_register(&module);
}
}  // namespace
