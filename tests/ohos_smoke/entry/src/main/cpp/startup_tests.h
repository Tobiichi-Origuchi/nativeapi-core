#pragma once
#include <multimodalinput/oh_input_manager.h>
#include "../../../../../../src/launch_at_login.h"
#include "../../../../../../src/capi/launch_at_login_c.h"
#include "../../../../../../src/platform/ohos/ohos_napi.h"

namespace nativeapi::tests {
inline void KeyboardPermissionProbe(const Input_KeyEvent*) {}
inline std::unique_ptr<LaunchAtLogin> startup_early;
inline native_launch_at_login_t startup_c = 0;
inline bool startup_uninitialized = false;
inline void InitializeStartupTest() {
  startup_early = std::make_unique<LaunchAtLogin>();
  startup_uninitialized = startup_early->GetId().empty() && startup_early->GetDisplayName().empty() &&
      !startup_early->IsEnabled() && !LaunchAtLogin::IsSupported();
  startup_c = native_launch_at_login_create();
  startup_uninitialized = startup_uninitialized && !native_launch_at_login_is_enabled(startup_c);
}
struct StartupWork {
  napi_async_work work = nullptr;
  napi_deferred deferred = nullptr;
  bool expected = false, sdk_supported = false, ui_before = false, c_ui_before = false;
  int passed = 0, failed = 0;
  void Check(bool ok, const char* name) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-startup", "%{public}s %{public}s", ok ? "PASS" : "FAIL", name);
  }
  void Run() {
    const auto monitor = OH_Input_AddKeyEventMonitor(KeyboardPermissionProbe);
    if (monitor == INPUT_SUCCESS) OH_Input_RemoveKeyEventMonitor(KeyboardPermissionProbe);
    OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-startup", "NATIVEAPI_INPUT_MONITOR_PERMISSION %{public}d", int(monitor));
    Check(monitor == INPUT_PERMISSION_DENIED, "ordinary fixture receives actual SDK input-monitor permission refusal");
    Check(startup_uninitialized, "metadata and status fail closed before runtime initialization");
    Check(!LaunchAtLogin::IsSupported() && !native_launch_at_login_is_supported(), "programmatic startup registration is unavailable through the normal SDK");
    const auto bundle = AppInfo::GetInstance().GetIdentifier();
    const auto name = AppInfo::GetInstance().GetName();
    Check(bundle == "dev.nativeapi.smoke" && !name.empty(), "startup defaults use actual attached bundle metadata");
    Check(startup_early->GetId() == bundle && startup_early->GetDisplayName() == name, "pre-initialization object resolves actual metadata after attachment");
    Check(startup_early->IsEnabled() == expected, "worker awaits actual self startup SDK result on retained C++ object");
    LaunchAtLogin self;
    Check(self.GetId() == bundle && self.GetDisplayName() == name, "default constructor uses actual bundle identifier and label");
    Check(self.IsEnabled() == expected, "fresh C++ worker getter agrees with independent direct SDK status");
    Check(!self.Enable() && !self.Disable() && self.IsEnabled() == expected, "unsupported mutators cannot change actual startup status");
    Check(self.SetDisplayName("Startup name 中文🙂") && self.GetDisplayName() == "Startup name 中文🙂", "display name is explicitly recorded local metadata");
    Check(!self.SetProgram("/data/storage/el2/base/files/app", {"--startup", "中文"}) && self.GetExecutablePath().empty() && self.GetArguments().empty(), "arbitrary executable and argument launch configuration is refused without local success");
    LaunchAtLogin named(bundle);
    Check(named.GetId() == bundle && named.GetDisplayName() == name && named.IsEnabled() == expected, "explicit self identifier queries only the current bundle");
    LaunchAtLogin foreign("dev.nativeapi.other", "Other");
    Check(foreign.GetId() == "dev.nativeapi.other" && foreign.GetDisplayName() == "Other" && !foreign.IsEnabled(), "foreign identity is not falsely relabeled as the self startup status");
    LaunchAtLogin empty("");
    Check(empty.GetId().empty() && !empty.IsEnabled(), "explicit empty identifier is refused rather than treated as self");
    auto c = native_launch_at_login_create();
    char* id = native_launch_at_login_get_id(c);
    char* label = native_launch_at_login_get_display_name(c);
    Check(c && id && bundle == id && label && name == label, "C ABI default metadata matches the packaged application");
    free_c_str(id); free_c_str(label);
    Check(native_launch_at_login_is_enabled(c) == expected && native_launch_at_login_is_enabled(startup_c) == expected, "C ABI worker getters return the real SDK state on fresh and retained objects");
    Check(!native_launch_at_login_enable(c) && !native_launch_at_login_disable(c) && native_launch_at_login_is_enabled(c) == expected, "C ABI mutators cannot fabricate an enabled or disabled state");
    Check(native_launch_at_login_set_display_name(c, "C startup 中文"), "C ABI records local display-name metadata");
    label = native_launch_at_login_get_display_name(c);
    Check(label && std::string(label) == "C startup 中文", "C ABI returns the recorded display-name value");
    free_c_str(label);
    auto args = native_launch_at_login_get_arguments(c);
    char* executable = native_launch_at_login_get_executable_path(c);
    Check(!executable && args.count == 0, "C ABI does not invent a desktop-style startup executable or arguments");
    Check(!native_launch_at_login_set_program(c, "/system/bin/appspawn", args), "C ABI refuses an arbitrary native startup executable");
    free_c_str(executable); native_string_list_free(&args);
    auto explicit_c = native_launch_at_login_create_with_id_and_display_name(bundle.c_str(), "C explicit");
    Check(explicit_c && native_launch_at_login_is_enabled(explicit_c) == expected, "C ABI explicit self constructor reads actual SDK startup status");
    native_launch_at_login_free(explicit_c);
    native_launch_at_login_free(c);
    Check(!native_launch_at_login_is_enabled(c) && !native_launch_at_login_enable(c), "released C startup handles fail safely");
    native_launch_at_login_free(c);
  }
};
inline napi_value StartupCheck(napi_env env, napi_callback_info info) {
  auto data = std::make_unique<StartupWork>();
  napi_value args[2]{}, promise = nullptr, name = nullptr;
  size_t count = 2;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  if (count != 2 || napi_get_value_bool(env, args[0], &data->sdk_supported) != napi_ok ||
      napi_get_value_bool(env, args[1], &data->expected) != napi_ok) return nullptr;
  data->ui_before = startup_early->IsEnabled();
  data->c_ui_before = native_launch_at_login_is_enabled(startup_c);
  if (napi_create_promise(env, &data->deferred, &promise) != napi_ok ||
      napi_create_string_utf8(env, "nativeapi.startup", NAPI_AUTO_LENGTH, &name) != napi_ok ||
      napi_create_async_work(env, nullptr, name,
        [](napi_env, void* raw) { static_cast<StartupWork*>(raw)->Run(); },
        [](napi_env env, napi_status status, void* raw) {
          std::unique_ptr<StartupWork> data(static_cast<StartupWork*>(raw));
          data->Check(status == napi_ok && IsMainThread(), "native startup worker completion returns to UI without a nested event loop");
          data->Check(startup_early->IsEnabled() == data->expected && native_launch_at_login_is_enabled(startup_c) == data->expected, "UI reads the completed native SDK snapshot after worker priming");
          std::ostringstream out;
          out << "{\"passed\":" << data->passed << ",\"failed\":" << data->failed
              << ",\"sdkSupported\":" << (data->sdk_supported ? "true" : "false")
              << ",\"expected\":" << (data->expected ? "true" : "false")
              << ",\"uiBefore\":" << (data->ui_before ? "true" : "false")
              << ",\"cUiBefore\":" << (data->c_ui_before ? "true" : "false") << '}';
          napi_value result = nullptr;
          napi_create_string_utf8(env, out.str().c_str(), NAPI_AUTO_LENGTH, &result);
          napi_resolve_deferred(env, data->deferred, result);
          napi_delete_async_work(env, data->work);
        }, data.get(), &data->work) != napi_ok) return nullptr;
  if (napi_queue_async_work(env, data->work) != napi_ok) {
    napi_delete_async_work(env, data->work);
    return nullptr;
  }
  data.release();
  return promise;
}
}  // namespace nativeapi::tests
