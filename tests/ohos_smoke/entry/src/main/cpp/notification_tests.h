#pragma once
#include <atomic>
#include <mutex>
#include <sstream>
#include <thread>
#include "../../../../../../src/capi/notification_manager_c.h"
#include "../../../../../../src/capi/string_utils_c.h"
#include "../../../../../../src/notification_manager.h"
#include "../../../../../../src/platform/ohos/ohos_napi.h"
#include "../../../../../../src/platform/ohos/ohos_notifications.h"

namespace nativeapi::tests {
inline bool notification_want_on_create = false;
inline std::string notification_create_argument;

class NotificationTests : public std::enable_shared_from_this<NotificationTests> {
 public:
  napi_env env = nullptr;
  napi_deferred deferred = nullptr;
  uv_timer_t timer{};
  std::mutex mutex;
  std::ostringstream report;
  int passed = 0, failed = 0, ticks = 0, phase = 0;
  size_t listener = 0;
  native_listener_id_t c_listener = 0;
  std::string mode = "normal", argument, c_argument;
  std::atomic<int> worker_stage{0};
  int cpp_events = 0, c_events = 0;
  bool threads_ok = true;
  bool snapshot_pending = false, snapshot_done = false;
  int snapshot_stage = 0;
  static napi_value SnapshotResult(napi_env env, napi_callback_info info) {
    size_t count = 2;
    napi_value args[2]{}, ignored = nullptr;
    void* data = nullptr;
    napi_get_cb_info(env, info, &count, args, nullptr, &data);
    auto self = *static_cast<std::shared_ptr<NotificationTests>*>(data);
    bool array = false;
    uint32_t length = 0;
    napi_valuetype error_type = napi_undefined;
    if (count)
      napi_typeof(env, args[0], &error_type);
    int32_t code = -1;
    napi_value code_value = nullptr;
    bool no_error = error_type == napi_undefined || error_type == napi_null;
    if (error_type == napi_number)
      no_error = napi_get_value_int32(env, args[0], &code) == napi_ok && code == 0;
    if (error_type == napi_object && ohos::js::Property(env, args[0], "code", code_value))
      no_error = napi_get_value_int32(env, code_value, &code) == napi_ok && code == 0;
    bool valid = count == 2 && no_error && napi_is_array(env, args[1], &array) == napi_ok &&
                 array && napi_get_array_length(env, args[1], &length) == napi_ok;
    int own = 0;
    bool content_ok = false;
    for (uint32_t i = 0; valid && i < length; ++i) {
      napi_value request = nullptr, content = nullptr, normal = nullptr;
      napi_get_element(env, args[1], i, &request);
      auto label = ohos::js::StringProperty(env, request, "label");
      if (label.rfind("nativeapi:", 0) != 0)
        continue;
      ++own;
      const bool warm = self->snapshot_stage == 1 && label == "nativeapi:probe";
      const bool cold_open = self->snapshot_stage == 3 && label == "nativeapi:cold_open";
      const bool cold_button = self->snapshot_stage == 4 && label == "nativeapi:cold_button";
      if ((warm || cold_open || cold_button) &&
          ohos::js::Property(env, request, "content", content) &&
          ohos::js::Property(env, content, "normal", normal)) {
        napi_value buttons = nullptr;
        uint32_t button_count = 0;
        content_ok = ohos::js::StringProperty(env, normal, "title") ==
                         (warm        ? "NativeAPI notification updated"
                          : cold_open ? "NativeAPI cold open"
                                      : "NativeAPI cold button") &&
                     ohos::js::StringProperty(env, normal, "text") ==
                         (warm ? "Unicode 中文 & exact tag replacement"
                               : "Restart the stopped NativeAPI test application");
        if (!cold_open)
          content_ok &= ohos::js::Property(env, request, "actionButtons", buttons) &&
                        napi_get_array_length(env, buttons, &button_count) == napi_ok &&
                        button_count == 1;
      }
    }
    if (self->snapshot_stage == 1)
      self->Check(valid && own == 1 && content_ok,
                  "actual SDK records prove exact tag replacement, content, action button and "
                  "independent removal");
    else if (self->snapshot_stage == 3 || self->snapshot_stage == 4)
      self->Check(valid && own == 1 && content_ok,
                  "actual SDK record retains the exact cold notification after Shutdown");
    else
      self->Check(valid && own == 0, "actual SDK records confirm notification cleanup");
    self->snapshot_done = true;
    napi_get_undefined(env, &ignored);
    return ignored;
  }
  void Snapshot(int stage) {
    snapshot_pending = true;
    snapshot_done = false;
    snapshot_stage = stage;
    napi_value module = nullptr, callback = nullptr, ignored = nullptr;
    auto* held = new std::shared_ptr<NotificationTests>(shared_from_this());
    if (napi_create_function(env, "notificationSnapshot", NAPI_AUTO_LENGTH, SnapshotResult, held,
                             &callback) != napi_ok) {
      delete held;
      Check(false, "snapshot callback creation");
      snapshot_done = true;
      return;
    }
    if (napi_add_finalizer(
            env, callback, held,
            [](napi_env, void* value, void*) {
              delete static_cast<std::shared_ptr<NotificationTests>*>(value);
            },
            nullptr, nullptr) != napi_ok) {
      delete held;
      Check(false, "snapshot finalizer registration");
      snapshot_done = true;
      return;
    }
    if (!ohos::js::Module(env, "@ohos.notificationManager", module) ||
        !ohos::js::Call(env, module, "getActiveNotifications", {callback}, ignored)) {
      Check(false, "SDK active notification query");
      snapshot_done = true;
    }
  }
  void Check(bool ok, const char* label) {
    std::lock_guard<std::mutex> lock(mutex);
    (ok ? passed : failed)++;
    report << (ok ? "PASS " : "FAIL ") << label << '\n';
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-notification",
                 "%{public}s %{public}s", ok ? "PASS" : "FAIL", label);
    if (!ok)
      OH_LOG_Print(LOG_APP, LOG_ERROR, 1, "nativeapi-notification", "SDK %{public}s",
                   NotificationManager::GetInstance().GetLastError().c_str());
  }
  void Listen() {
    auto& manager = NotificationManager::GetInstance();
    listener = manager.AddListener<NotificationActivatedEvent>(
        [self = shared_from_this()](const auto& event) {
          self->argument = event.GetArgument();
          ++self->cpp_events;
          self->threads_ok &= IsMainThread();
          OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-notification", "ACTIVATION %{public}s",
                       event.GetArgument().c_str());
        });
    c_listener = native_notification_manager_add_listener(
        [](const native_notification_event_t* event, void* data) {
          auto* self = static_cast<NotificationTests*>(data);
          if (event->type == NATIVE_NOTIFICATION_EVENT_TYPE_ACTIVATED) {
            ++self->c_events;
            self->threads_ok &= IsMainThread() && event->data.activated.argument;
            self->c_argument = event->data.activated.argument ? event->data.activated.argument : "";
          }
        },
        this, nullptr);
  }
  void BeginLifecycle() {
    Check(NotificationManager::IsSupported(),
          "public notification SDK is present for lifecycle testing");
    if (mode == "denied") {
      napi_value module = nullptr, enabled = nullptr;
      bool allowed = true;
      const bool queried = ohos::js::Module(env, "@ohos.notificationManager", module) &&
                           ohos::js::Call(env, module, "isNotificationEnabledSync", {}, enabled) &&
                           napi_get_value_bool(env, enabled, &allowed) == napi_ok;
      Check(queried && !allowed, "fresh denial fixture has no preexisting notification grant");
    }
    Listen();
    const bool recover = mode.rfind("recover-", 0) == 0;
    if (recover) {
      const auto expected =
          mode == "recover-open" ? "tag=cold_open&action=open" : "tag=cold_button&action=button";
      Check(notification_want_on_create && notification_create_argument == expected,
            "original system notification Want arrived through onCreate before Initialize");
    }
    OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-notification",
                 "NATIVEAPI_NOTIFICATION_AUTHORIZING");
    auto self = shared_from_this();
    std::thread([self, recover] {
      auto& m = NotificationManager::GetInstance();
      const bool use_c = self->mode == "denied" || self->mode == "recover-button";
      bool initialized = use_c ? native_notification_manager_initialize() : m.Initialize();
      if (self->mode == "denied") {
        const auto error = m.GetLastError();
        char* c_error = native_notification_manager_get_last_error();
        self->Check(
            !initialized && error.find("Notification SDK error (") == 0 && c_error &&
                error == c_error,
            "real authorization refusal returns false and the same owned C ABI SDK diagnostic");
        free_c_str(c_error);
        OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-notification", "DENIED_SDK_ERROR %{public}s",
                     error.c_str());
        if (initialized) {
          self->worker_stage = -1;
          return;
        }
        self->Check(
            !m.Show("Denied", "must not publish", "denied", "") &&
                !native_notification_manager_show("Denied", "must not publish", "denied", ""),
            "denied initialization leaves C++ and C ABI publication disabled");
        self->worker_stage = 1;
        return;
      }
      self->Check(initialized, "notification lifecycle initializes through actual SDK completion");
      if (!initialized) {
        self->worker_stage = -1;
        return;
      }
      if (!recover) {
        m.Remove("cold_open");
        m.Remove("cold_button");
        bool button = self->mode == "prepare-button";
        self->Check(
            button ? native_notification_manager_show(
                         "NativeAPI cold button", "Restart the stopped NativeAPI test application",
                         "cold_button", "NativeAPI cold action")
                   : m.Show("NativeAPI cold open", "Restart the stopped NativeAPI test application",
                            "cold_open", ""),
            "publish a real cold-launch notification with the expected stable tag");
        m.Shutdown();
        self->Check(
            !m.Show("After Shutdown", "must not publish", "guard", "") &&
                !m.Remove(button ? "cold_button" : "cold_open"),
            "Shutdown disables manager calls while the system notification remains independent");
      }
      self->worker_stage = 1;
    }).detach();
  }
  void TickLifecycle() {
    if (worker_stage != 1)
      return;
    if (mode == "denied") {
      napi_value module = nullptr, enabled = nullptr;
      bool allowed = true;
      const bool queried = ohos::js::Module(env, "@ohos.notificationManager", module) &&
                           ohos::js::Call(env, module, "isNotificationEnabledSync", {}, enabled) &&
                           napi_get_value_bool(env, enabled, &allowed) == napi_ok;
      Check(queried && !allowed && cpp_events == 0 && c_events == 0,
            "system authorization remains disabled and emits no activation");
      Finish();
      return;
    }
    if (mode.rfind("prepare-", 0) == 0) {
      if (!snapshot_pending)
        Snapshot(mode == "prepare-open" ? 3 : 4);
      if (snapshot_done)
        Finish();
      return;
    }
    if (phase == 0 && cpp_events >= 1 && c_events >= 1) {
      const auto expected =
          mode == "recover-open" ? "tag=cold_open&action=open" : "tag=cold_button&action=button";
      Check(argument == expected && c_argument == argument && cpp_events == 1 && c_events == 1 &&
                threads_ok,
            "cold launch delivers the queued original argument exactly once through C++ and C ABI "
            "on UI thread");
      phase = 1;
      worker_stage = 0;
      auto self = shared_from_this();
      std::thread([self] {
        NotificationManager::GetInstance().Remove(self->mode == "recover-open" ? "cold_open"
                                                                               : "cold_button");
        self->worker_stage = 1;
      }).detach();
    } else if (phase == 1) {
      if (!snapshot_pending)
        Snapshot(2);
      if (snapshot_done)
        Finish();
    }
  }
  void Begin() {
    OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-notification",
                 "NATIVEAPI_NOTIFICATION_MODE %{public}s", mode.c_str());
    if (mode != "normal") {
      BeginLifecycle();
      return;
    }
    auto& manager = NotificationManager::GetInstance();
    Check(NotificationManager::IsSupported() && native_notification_manager_is_supported(),
          "SDK notifications are available through native and C ABI");
    Check(!manager.Show("before", "init", "guard", "") && !manager.GetLastError().empty(),
          "unregistered Show fails with a diagnostic");
    Check(!manager.Initialize() && manager.GetLastError().find("worker") != std::string::npos,
          "UI-thread initialization fails explicitly");
    Listen();
    OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-notification",
                 "NATIVEAPI_NOTIFICATION_AUTHORIZING");
    auto self = shared_from_this();
    std::thread([self] {
      auto& m = NotificationManager::GetInstance();
      bool initialized = m.Initialize();
      self->Check(initialized, "worker initialization waits for actual notification authorization");
      if (!initialized) {
        self->worker_stage = -1;
        return;
      }
      self->Check(native_notification_manager_initialize(),
                  "repeated C ABI Initialize is idempotent");
      // Remove only this fixture's known tags after an interrupted prior run.
      for (const auto* tag : {"probe", "body", "temporary", "retained"})
        m.Remove(tag);
      self->Check(!m.Show("title", "message", "", "") && !m.Remove(std::string(193, 'x')) &&
                      !m.Show("title", "message", std::string("a\0b", 3), ""),
                  "empty, overlong and embedded-NUL tags are rejected");
      self->Check(!m.Show("", "message", "invalid", "") && !m.GetLastError().empty(),
                  "invalid content retains an explicit diagnostic");
      self->Check(m.Show("NativeAPI original", "replace this notification", "probe", ""),
                  "SDK accepts original tagged notification");
      std::this_thread::sleep_for(std::chrono::milliseconds(1100));
      self->Check(m.Show("NativeAPI notification updated", "Unicode 中文 & exact tag replacement",
                         "probe", "NativeAPI action"),
                  "same tag replaces notification with a native action button");
      std::this_thread::sleep_for(std::chrono::milliseconds(1100));
      self->Check(native_notification_manager_show("NativeAPI temporary", "remove only this tag",
                                                   "temporary", ""),
                  "C ABI publishes independent notification");
      self->Check(native_notification_manager_remove("temporary"),
                  "C ABI removes its own tag without removing other notification");
      self->Check(m.GetLastError().empty(), "successful SDK operation clears prior diagnostic");
      self->worker_stage = 1;
    }).detach();
  }
  void Tick() {
    if (++ticks > 6000 || worker_stage == -1) {
      Check(false, "notification interaction deadline or initialization failure");
      Finish();
      return;
    }
    if (mode != "normal") {
      TickLifecycle();
      return;
    }
    if (phase == 0 && worker_stage == 1) {
      if (!snapshot_pending)
        Snapshot(1);
      if (!snapshot_done)
        return;
      phase = 1;
      OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-notification",
                   "NATIVEAPI_NOTIFICATION_BUTTON_READY");
    } else if (phase == 1 && cpp_events >= 1) {
      Check(argument == "tag=probe&action=button" && c_argument == argument && cpp_events == 1 &&
                c_events == 1 && threads_ok,
            "real notification button reaches native and C listeners once on UI thread");
      phase = 2;
      worker_stage = 0;
      auto self = shared_from_this();
      std::thread([self] {
        auto& m = NotificationManager::GetInstance();
        m.Remove("probe");
        self->Check(m.Show("NativeAPI open notification", "Tap this body to activate", "body", ""),
                    "body-only notification is published");
        self->worker_stage = 2;
      }).detach();
    } else if (phase == 2 && worker_stage == 2) {
      phase = 3;
      OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-notification",
                   "NATIVEAPI_NOTIFICATION_BODY_READY");
    } else if (phase == 3 && cpp_events >= 2) {
      Check(argument == "tag=body&action=open" && c_argument == argument && cpp_events == 2 &&
                c_events == 2 && threads_ok,
            "real notification body reaches both listeners with open argument");
      phase = 4;
      worker_stage = 0;
      auto self = shared_from_this();
      std::thread([self] {
        auto& m = NotificationManager::GetInstance();
        m.Remove("body");
        self->Check(m.Show("NativeAPI retained", "Shutdown must not remove delivered notifications",
                           "retained", ""),
                    "publish notification before Shutdown");
        m.Shutdown();
        self->Check(!m.Show("x", "y", "after", "") && !m.Remove("retained"),
                    "Shutdown disables publishing and removal until reinitialization");
        self->Check(m.Initialize(), "notifications can reinitialize after Shutdown");
        self->Check(m.Remove("retained"),
                    "reinitialized manager removes retained notification by stable tag");
        self->worker_stage = 3;
      }).detach();
    } else if (phase == 4 && worker_stage == 3) {
      if (snapshot_stage != 2)
        Snapshot(2);
      if (snapshot_done)
        Finish();
    }
  }
  void Finish() {
    auto& m = NotificationManager::GetInstance();
    m.RemoveListener(listener);
    native_notification_manager_remove_listener(c_listener);
    m.Shutdown();
    std::lock_guard<std::mutex> lock(mutex);
    const auto marker = failed ? "NATIVEAPI_NOTIFICATION_FAIL" : "NATIVEAPI_NOTIFICATION_PASS";
    report << marker << " (" << passed << " passed, " << failed << " failed)\n";
    OH_LOG_Print(LOG_APP, failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-notification",
                 "%{public}s (%{public}d passed, %{public}d failed)", marker, passed, failed);
    napi_handle_scope scope;
    napi_open_handle_scope(env, &scope);
    napi_value value;
    auto text = report.str();
    napi_create_string_utf8(env, text.data(), text.size(), &value);
    napi_resolve_deferred(env, deferred, value);
    napi_close_handle_scope(env, scope);
    uv_timer_stop(&timer);
    uv_close(reinterpret_cast<uv_handle_t*>(&timer), [](uv_handle_t* h) {
      delete static_cast<std::shared_ptr<NotificationTests>*>(h->data);
    });
  }
};
inline napi_value RunNotifications(napi_env env, napi_callback_info info) {
  auto self = std::make_shared<NotificationTests>();
  self->env = env;
  size_t count = 1;
  napi_value mode = nullptr;
  napi_get_cb_info(env, info, &count, &mode, nullptr, nullptr);
  if (count)
    self->mode = ohos::js::String(env, mode);
  if (self->mode.empty())
    self->mode = "normal";
  napi_value promise;
  napi_create_promise(env, &self->deferred, &promise);
  uv_loop_t* loop = nullptr;
  napi_get_uv_event_loop(env, &loop);
  uv_timer_init(loop, &self->timer);
  self->timer.data = new std::shared_ptr<NotificationTests>(self);
  uv_timer_start(
      &self->timer,
      [](uv_timer_t* timer) {
        (*static_cast<std::shared_ptr<NotificationTests>*>(timer->data))->Tick();
      },
      100, 100);
  self->Begin();
  return promise;
}
inline napi_value HandleNotificationWant(napi_env env, napi_callback_info info) {
  size_t count = 2;
  napi_value args[2]{}, value = nullptr;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  bool created = false;
  if (count > 1)
    napi_get_value_bool(env, args[1], &created);
  if (count && created) {
    napi_value parameters = nullptr;
    if (ohos::js::Property(env, args[0], "parameters", parameters)) {
      notification_create_argument =
          ohos::js::StringProperty(env, parameters, "nativeapi.notification.argument");
      notification_want_on_create = !notification_create_argument.empty();
      if (notification_want_on_create)
        OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-notification",
                     "NATIVEAPI_NOTIFICATION_WANT_ON_CREATE %{public}s",
                     notification_create_argument.c_str());
    }
  }
  napi_get_boolean(env, count && ohos::HandleNotificationWant(env, args[0]), &value);
  return value;
}
}  // namespace nativeapi::tests
