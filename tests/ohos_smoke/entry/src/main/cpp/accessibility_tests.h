#pragma once
#include <atomic>
#include <mutex>
#include <sstream>
#include <thread>
#include "../../../../../../src/accessibility_manager.h"
#include "../../../../../../src/capi/accessibility_manager_c.h"
#include "../../../../../../src/foundation/dispatcher.h"

namespace nativeapi::tests {
inline bool accessibility_uninitialized_closed = false;
class AccessibilityTests : public std::enable_shared_from_this<AccessibilityTests> {
 public:
  napi_env env = nullptr;
  napi_deferred deferred = nullptr;
  uv_timer_t timer{};
  bool expected = false;
  std::string run_id;
  int enabled_count = 0, passed = 0, failed = 0, ticks = 0;
  std::atomic<bool> completed{false};
  std::mutex mutex;
  std::ostringstream report;
  void Check(bool ok, const char* label) {
    std::lock_guard<std::mutex> lock(mutex);
    (ok ? passed : failed)++;
    report << (ok ? "PASS " : "FAIL ") << label << '\n';
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-access", "%{public}s %{public}s",
                 ok ? "PASS" : "FAIL", label);
  }
  void Start() {
    OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-access", "NATIVEAPI_ACCESSIBILITY_RUN %{public}s", run_id.c_str());
    OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-access",
                 "SDK_REFERENCE enabled=%{public}d enabled_extensions=%{public}d", expected, enabled_count);
    Check(accessibility_uninitialized_closed, "uninitialized C++ and C ABI queries fail closed before and after Enable");
    Check(IsMainThread(), "accessibility suite begins on initialized ArkTS UI thread");
    Check(expected == (enabled_count > 0), "independent SDK state agrees with enabled extension list");
    auto& manager = AccessibilityManager::GetInstance();
    Check(manager.IsEnabled() == expected, "C++ reads actual SDK accessibility state on UI thread");
    Check(native_accessibility_manager_is_enabled() == expected, "C ABI reads actual SDK accessibility state on UI thread");
    manager.Enable();
    manager.Enable();
    Check(manager.IsEnabled() == expected, "repeated C++ Enable does not fabricate system authorization");
    native_accessibility_manager_enable();
    Check(native_accessibility_manager_is_enabled() == expected, "C ABI Enable preserves real system state");
    std::thread([self = shared_from_this()] {
      self->Check(!IsMainThread(), "worker checks execute off the UI thread");
      auto& manager = AccessibilityManager::GetInstance();
      self->Check(manager.IsEnabled() == self->expected, "C++ worker query marshals to real SDK UI environment");
      self->Check(native_accessibility_manager_is_enabled() == self->expected, "C ABI worker query marshals to real SDK UI environment");
      manager.Enable();
      native_accessibility_manager_enable();
      self->Check(manager.IsEnabled() == self->expected, "worker Enable calls do not alter queried system state");
      self->completed = true;
    }).detach();
  }
  void Tick() {
    if (!completed && ++ticks < 200)
      return;
    if (!completed)
      Check(false, "worker accessibility query completes before deadline");
    Check(AccessibilityManager::GetInstance().IsEnabled() == expected,
          "UI query still observes system state after worker calls");
    std::lock_guard<std::mutex> lock(mutex);
    const char* marker = failed ? "NATIVEAPI_ACCESSIBILITY_FAIL" : "NATIVEAPI_ACCESSIBILITY_PASS";
    report << marker << " (" << passed << " passed, " << failed << " failed)\n";
    OH_LOG_Print(LOG_APP, failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-access",
                 "%{public}s (%{public}d passed, %{public}d failed)", marker, passed, failed);
    napi_handle_scope scope;
    napi_open_handle_scope(env, &scope);
    napi_value value;
    const auto text = report.str();
    napi_create_string_utf8(env, text.data(), text.size(), &value);
    napi_resolve_deferred(env, deferred, value);
    napi_close_handle_scope(env, scope);
    uv_timer_stop(&timer);
    uv_close(reinterpret_cast<uv_handle_t*>(&timer), [](uv_handle_t* handle) {
      delete static_cast<std::shared_ptr<AccessibilityTests>*>(handle->data);
    });
  }
};
inline napi_value RunAccessibility(napi_env env, napi_callback_info info) {
  auto tests = std::make_shared<AccessibilityTests>();
  tests->env = env;
  napi_value args[3]{};
  size_t count = 3;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  if (count != 3 || napi_get_value_bool(env, args[0], &tests->expected) != napi_ok ||
      napi_get_value_int32(env, args[1], &tests->enabled_count) != napi_ok || tests->enabled_count < 0) {
    napi_throw_type_error(env, nullptr, "Expected the real SDK boolean, enabled extension count and run ID");
    return nullptr;
  }
  tests->run_id = ohos::js::String(env, args[2]);
  if (tests->run_id.empty()) {
    napi_throw_type_error(env, nullptr, "Missing unique run ID");
    return nullptr;
  }
  napi_value promise;
  napi_create_promise(env, &tests->deferred, &promise);
  uv_loop_t* loop = nullptr;
  napi_get_uv_event_loop(env, &loop);
  uv_timer_init(loop, &tests->timer);
  tests->timer.data = new std::shared_ptr<AccessibilityTests>(tests);
  uv_timer_start(&tests->timer, [](uv_timer_t* timer) {
    (*static_cast<std::shared_ptr<AccessibilityTests>*>(timer->data))->Tick();
  }, 100, 100);
  tests->Start();
  return promise;
}
}  // namespace nativeapi::tests
