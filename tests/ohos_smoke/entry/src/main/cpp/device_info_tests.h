#pragma once
#include "../../../../../../src/device_info.h"
#include "../../../../../../src/capi/device_info_c.h"
#include "../../../../../../src/platform/ohos/ohos_napi.h"
#include <array>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <thread>

namespace nativeapi_device_info_fault_probe {
std::pair<int, int> RunControlledDeviceInfoFaults(napi_env env);
}
namespace nativeapi::tests {
using DeviceSnapshot = std::array<std::string, 7>;
inline DeviceSnapshot ReadDeviceCpp() {
  auto& d = DeviceInfo::GetInstance();
  return {d.GetName(), d.GetModel(), d.GetManufacturer(), d.GetOsName(),
          d.GetOsVersion(), d.GetKernelVersion(), d.GetArchitecture()};
}
inline DeviceSnapshot ReadDeviceC() {
  const std::array<char* (*)(), 7> getters{native_device_info_get_name,
      native_device_info_get_model, native_device_info_get_manufacturer,
      native_device_info_get_os_name, native_device_info_get_os_version,
      native_device_info_get_kernel_version, native_device_info_get_architecture};
  DeviceSnapshot values;
  for (size_t i = 0; i < getters.size(); ++i) {
    char* value = getters[i]();
    values[i] = value ? value : "";
    free_c_str(value);
  }
  return values;
}
inline DeviceSnapshot device_early, device_c_early;
inline std::pair<int, int> device_faults;
inline void InitializeDeviceInfoTest(napi_env env) {
  device_early = ReadDeviceCpp();
  device_c_early = ReadDeviceC();
  device_faults = nativeapi_device_info_fault_probe::RunControlledDeviceInfoFaults(env);
}
inline napi_value DeviceArray(napi_env env, const DeviceSnapshot& snapshot) {
  napi_value array = nullptr;
  napi_create_array_with_length(env, snapshot.size(), &array);
  for (size_t i = 0; i < snapshot.size(); ++i) {
    napi_value text = nullptr;
    napi_create_string_utf8(env, snapshot[i].data(), snapshot[i].size(), &text);
    napi_set_element(env, array, i, text);
  }
  return array;
}
struct DeviceWork {
  napi_async_work work = nullptr;
  napi_deferred deferred = nullptr;
  DeviceSnapshot cpp, c, worker;
  std::array<unsigned char, 20> elf{};
  int passed = 0, failed = 0, samples = 0;
  std::mutex mutex;
  void Check(bool ok, const char* text) {
    std::lock_guard<std::mutex> lock(mutex);
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-device",
                 "%{public}s %{public}s", ok ? "PASS" : "FAIL", text);
  }
  void Run() {
    Check(!IsMainThread(), "device async work uses a real worker");
    std::array<std::thread, 4> readers;
    for (size_t i = 0; i < readers.size(); ++i) readers[i] = std::thread([&, i] {
      for (int round = 0; round < 5; ++round) {
        const auto native = ReadDeviceCpp();
        const auto c_native = ReadDeviceC();
        for (size_t field = 0; field < native.size(); ++field)
          Check(native[field] == cpp[field] && c_native[field] == cpp[field],
                  "concurrent C++/C getters preserve actual UI snapshot values");
        if (i == 0 && round == 0) worker = native;
        std::lock_guard<std::mutex> lock(mutex);
        ++samples;
      }
    });
    for (auto& reader : readers) reader.join();
  }
};
inline napi_value DeviceInfoCheck(napi_env env, napi_callback_info) {
  auto data = std::make_unique<DeviceWork>();
  data->cpp = ReadDeviceCpp(); data->c = ReadDeviceC();
  data->Check(IsMainThread(), "device getters run on the owning UI thread");
  for (size_t i = 0; i < data->cpp.size(); ++i) {
    data->Check(data->cpp[i] == data->c[i], "all UI C getters match their C++ values");
    data->Check(device_early[i] == device_c_early[i], "all device getters work before host initialization through C++ and C");
    if (i) data->Check(device_early[i] == data->cpp[i], "non-context device metadata remains valid after host attachment");
  }
  data->Check(device_early[0] == (OH_GetMarketName() ? OH_GetMarketName() : ""),
               "pre-host device name uses only the public NDK market name");
  char* first = native_device_info_get_model();
  char* second = native_device_info_get_model();
  if (first && first[0]) first[0] = first[0] == 'X' ? 'Y' : 'X';
  data->Check((second ? second : "") == data->cpp[1] &&
                DeviceInfo::GetInstance().GetModel() == data->cpp[1],
               "C caller owns an independent device string allocation");
  free_c_str(first); free_c_str(second);
  data->Check(device_faults.second == 0 && device_faults.first >= 25,
               "actual backend passes controlled SDK null/empty/change/uname failure and process-ABI checks");
  // The shell cannot read this app's /proc/PID/exe on these images. Inspect
  // the actual loaded core library's ELF identity in the owning process instead.
  Dl_info image{};
  if (dladdr(reinterpret_cast<const void*>(&native_device_info_get_architecture), &image) && image.dli_fbase)
    std::memcpy(data->elf.data(), image.dli_fbase, data->elf.size());
  data->Check(data->elf[0] == 127 && data->elf[1] == 'E' && data->elf[2] == 'L' && data->elf[3] == 'F',
               "independent OS loader image exposes actual native ELF identity");
  bool context_seen = ohos::WithContext([&](napi_env current, napi_value context) {
    const bool detached = ohos::DetachContext(current, context);
    data->Check(detached, "detach exactly the owning device-info context");
    if (!detached) return;
    try {
      const auto without = ReadDeviceCpp();
      const auto c_without = ReadDeviceC();
      const std::string market = OH_GetMarketName() ? OH_GetMarketName() : "";
      data->Check(without[0] == market && c_without[0] == market,
                   "detached C++/C device names use the actual NDK fallback");
      for (size_t i = 1; i < without.size(); ++i)
        data->Check(without[i] == data->cpp[i] && c_without[i] == data->cpp[i],
                     "NDK device metadata remains readable while the context is detached");
    } catch (...) {
      ohos::AttachContext(current, context);
      throw;
    }
    data->Check(ohos::AttachContext(current, context), "restore owning context after fallback checks");
  });
  data->Check(context_seen && ReadDeviceCpp() == data->cpp && ReadDeviceC() == data->c,
               "reattached getters immediately return current Settings metadata");
  napi_value promise = nullptr, name = nullptr;
  napi_create_promise(env, &data->deferred, &promise);
  napi_create_string_utf8(env, "nativeapi.device-info", NAPI_AUTO_LENGTH, &name);
  const auto status = napi_create_async_work(env, nullptr, name,
    [](napi_env, void* raw) { static_cast<DeviceWork*>(raw)->Run(); },
    [](napi_env env, napi_status status, void* raw) {
      std::unique_ptr<DeviceWork> data(static_cast<DeviceWork*>(raw));
      data->Check(status == napi_ok && IsMainThread(), "device completion returns to UI without nesting its loop");
      napi_value object = nullptr, global = nullptr, json = nullptr, result = nullptr;
      napi_create_object(env, &object);
      for (auto pair : {std::make_pair("passed", data->passed), {"failed", data->failed},
                       {"samples", data->samples}, {"faultsPassed", device_faults.first}})
        napi_set_named_property(env, object, pair.first, ohos::js::Number(env, pair.second));
      for (auto pair : {std::make_pair("cpp", &data->cpp), {"c", &data->c},
                       {"worker", &data->worker}, {"pre", &device_early}, {"preC", &device_c_early}})
        napi_set_named_property(env, object, pair.first, DeviceArray(env, *pair.second));
      napi_value elf = nullptr;
      napi_create_array_with_length(env, data->elf.size(), &elf);
      for (size_t i = 0; i < data->elf.size(); ++i)
        napi_set_element(env, elf, i, ohos::js::Number(env, data->elf[i]));
      napi_set_named_property(env, object, "elf", elf);
      napi_set_named_property(env, object, "pointerBits", ohos::js::Number(env, sizeof(void*) * 8));
      napi_get_global(env, &global);
      if (ohos::js::Property(env, global, "JSON", json) &&
          ohos::js::Call(env, json, "stringify", {object}, result))
        napi_resolve_deferred(env, data->deferred, result);
      else napi_reject_deferred(env, data->deferred, object);
      napi_delete_async_work(env, data->work);
    }, data.get(), &data->work);
  if (status != napi_ok) return nullptr;
  if (napi_queue_async_work(env, data->work) != napi_ok) {
    napi_delete_async_work(env, data->work);
    return nullptr;
  }
  data.release(); return promise;
}
}  // namespace nativeapi::tests
