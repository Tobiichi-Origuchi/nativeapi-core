// This target compiles the actual backend in a separate namespace, with only
// the SDK reads/uname substituted. No test hook or fake symbol enters production.
#include "../../../../../../src/device_info.h"
#include "../../../../../../src/platform/ohos/ohos_runtime.h"
#include <deviceinfo.h>
#include <sys/utsname.h>
#include <array>
#include <cstring>
#include <mutex>
#include <optional>

namespace {
std::array<std::optional<std::string>, 6> values;
bool uname_ok = true;
napi_env sdk_env = nullptr;
bool context_enabled = false, load_error = false, undefined_module = false, missing_method = false;
enum class SettingsResult { String, Throw, Number };
SettingsResult settings_result = SettingsResult::String;
std::string settings_name;
const char* Value(size_t index) { return values[index] ? values[index]->c_str() : nullptr; }
napi_value SettingsRead(napi_env env, napi_callback_info) {
  if (settings_result == SettingsResult::Throw) {
    napi_throw_error(env, "201", "controlled Settings refusal");
    return nullptr;
  }
  napi_value result = nullptr;
  if (settings_result == SettingsResult::Number) napi_create_int32(env, 123, &result);
  else napi_create_string_utf8(env, settings_name.data(), settings_name.size(), &result);
  return result;
}
}
extern "C" {
napi_status napi_load_module(napi_env env, const char*, napi_value* result) {
  if (load_error) {
    napi_throw_error(env, nullptr, "controlled missing Settings module");
    return napi_pending_exception;
  }
  if (undefined_module) return napi_get_undefined(env, result);
  napi_value general = nullptr, key = nullptr, method = nullptr;
  napi_create_object(env, result);
  napi_create_object(env, &general);
  napi_create_string_utf8(env, "controlled_device_name", NAPI_AUTO_LENGTH, &key);
  napi_set_named_property(env, general, "DEVICE_NAME", key);
  napi_set_named_property(env, *result, "general", general);
  if (!missing_method) {
    napi_create_function(env, "getValueSync", NAPI_AUTO_LENGTH, SettingsRead, nullptr, &method);
    napi_set_named_property(env, *result, "getValueSync", method);
  }
  return napi_ok;
}
const char* OH_GetMarketName() { return Value(0); }
const char* OH_GetProductModel() { return Value(1); }
const char* OH_GetManufacture() { return Value(2); }
const char* OH_GetDistributionOSName() { return Value(3); }
const char* OH_GetDistributionOSVersion() { return Value(4); }
const char* OH_GetOSFullName() { return Value(5); }
int uname(struct utsname* result) {
  if (!uname_ok) return -1;
  std::memset(result, 0, sizeof(*result));
  std::strcpy(result->release, "test-kernel-42");
  std::strcpy(result->machine, "different-kernel-machine");
  return 0;
}
}
namespace nativeapi::ohos {
bool WithContext(const std::function<void(napi_env, napi_value)>& work) {
  if (!context_enabled || !sdk_env) return false;
  napi_handle_scope scope = nullptr;
  if (napi_open_handle_scope(sdk_env, &scope) != napi_ok) return false;
  napi_value context = nullptr;
  napi_create_object(sdk_env, &context);
  work(sdk_env, context);
  napi_close_handle_scope(sdk_env, scope);
  return true;
}
}
namespace nativeapi {
std::pair<int, int> RunControlledDeviceInfoFaults(napi_env env) {
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  int passed = 0, failed = 0;
  auto check = [&](bool ok) { (ok ? passed : failed)++; };
  auto& device = DeviceInfo::GetInstance();
  values = {"Market 中文🙂", "Product-1", "Vendor-1", "RealOS", "1.2.3", "FullOS-1.2.3"};
  check(device.GetName() == *values[0]);
  check(device.GetModel() == *values[1]);
  check(device.GetManufacturer() == *values[2]);
  check(device.GetOsName() == *values[3]);
  check(device.GetOsVersion() == *values[4]);
  const auto held = device.GetName();
  values[0] = "Changed market";
  check(device.GetName() == *values[0] && held == "Market 中文🙂");
  values[1] = "Product-2";
  check(device.GetModel() == "Product-2");
  values[2] = "Vendor-2";
  check(device.GetManufacturer() == "Vendor-2");
  values[3] = "ChangedOS";
  check(device.GetOsName() == "ChangedOS");
  values[4] = "2.3.4";
  check(device.GetOsVersion() == "2.3.4");
  const std::array<std::string (DeviceInfo::*)() const, 4> getters{
      &DeviceInfo::GetName, &DeviceInfo::GetModel,
      &DeviceInfo::GetManufacturer, &DeviceInfo::GetOsName};
  for (size_t i = 0; i < getters.size(); ++i) {
    values[i].reset();
    check((device.*getters[i])().empty());
    values[i] = "";
    check((device.*getters[i])().empty());
  }
  values[4].reset();
  check(device.GetOsVersion() == "FullOS-1.2.3");
  values[4] = "";
  check(device.GetOsVersion() == "FullOS-1.2.3");
  values[5].reset();
  check(device.GetOsVersion().empty());
  values[5] = "";
  check(device.GetOsVersion().empty());
  check(device.GetKernelVersion() == "test-kernel-42");
  const auto architecture = device.GetArchitecture();
#if defined(__aarch64__)
  check(architecture == "aarch64");
#elif defined(__x86_64__)
  check(architecture == "x86_64");
#elif defined(__arm__)
  check(architecture == "arm");
#elif defined(__i386__)
  check(architecture == "i686");
#else
  check(architecture.empty());
#endif
  check(architecture != "different-kernel-machine");
  uname_ok = false;
  check(device.GetKernelVersion().empty());
  check(device.GetArchitecture() == architecture);
  uname_ok = true;
  // The real N-API engine handles these isolated SDK-shaped objects. Only this
  // separately compiled backend's module loader is substituted; the actual
  // application's SDK module and production runtime are never modified.
  sdk_env = env; context_enabled = true;
  values[0] = "Fallback market";
  settings_name = "User 中文🙂";
  check(device.GetName() == settings_name);
  const auto held_user_name = device.GetName();
  settings_name = "Changed user name";
  check(device.GetName() == settings_name && held_user_name == "User 中文🙂");
  settings_name = "";
  check(device.GetName() == "Fallback market");
  auto no_exception = [&] {
    bool pending = true;
    return napi_is_exception_pending(env, &pending) == napi_ok && !pending;
  };
  settings_result = SettingsResult::Throw;
  check(device.GetName() == "Fallback market");
  check(no_exception());
  settings_result = SettingsResult::String;
  load_error = true;
  check(device.GetName() == "Fallback market");
  check(no_exception());
  load_error = false;
  settings_result = SettingsResult::Number;
  check(device.GetName() == "Fallback market");
  check(no_exception());
  settings_result = SettingsResult::String;
  undefined_module = true;
  check(device.GetName() == "Fallback market");
  undefined_module = false;
  missing_method = true;
  check(device.GetName() == "Fallback market");
  check(no_exception());
  missing_method = false;
  context_enabled = false; sdk_env = nullptr;
  return {passed, failed};
}
}
