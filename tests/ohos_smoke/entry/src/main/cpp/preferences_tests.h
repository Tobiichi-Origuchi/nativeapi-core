#pragma once
#include "../../../../../../src/preferences.h"
#include "../../../../../../src/capi/preferences_c.h"
#include "../../../../../../src/platform/ohos/ohos_napi.h"
#include <algorithm>
#include <array>
#include <mutex>
#include <atomic>
#include <thread>

namespace nativeapi::tests {
inline std::string PreferenceString(char* value) {
  const std::string result = value ? value : "";
  free_c_str(value);
  return result;
}
inline std::map<std::string, std::string> PreferenceMap(native_preferences_t handle) {
  auto values = native_preferences_get_all(handle);
  std::map<std::string, std::string> result;
  for (long i = 0; i < values.count; ++i)
    result.emplace(values.keys[i] ? values.keys[i] : "", values.values[i] ? values.values[i] : "");
  native_string_map_free(&values);
  return result;
}
struct PreferencesWork {
  napi_async_work work = nullptr;
  napi_deferred deferred = nullptr;
  std::string command, scope;
  std::map<std::string, std::string> values;
  std::mutex mutex;
  int passed = 0, failed = 0;
  void Check(bool ok, const char* message) {
    std::lock_guard<std::mutex> lock(mutex);
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-preferences",
      "%{public}s %{public}s", ok ? "PASS" : "FAIL", message);
  }
  void Run() {
    Check(!IsMainThread(), "preferences run on a real worker");
    // All mutations are confined to the runner's unique disposable scope.
    Check(scope.rfind("nativeapi-pref-test-", 0) == 0 && scope.size() < 70,
          "bounded disposable preference scope");
    if (scope.rfind("nativeapi-pref-test-", 0) != 0 || scope.size() >= 70) return;
    Preferences a(scope), b(scope), defaults;
    auto c = native_preferences_create_with_scope(scope.c_str());
    auto c_default = native_preferences_create();
    Check(a.GetScope() == scope && b.GetScope() == scope &&
      PreferenceString(native_preferences_get_scope(c)) == scope,
      "C++ and C constructors preserve exact custom scope");
    Check(defaults.GetScope() == "default" &&
      PreferenceString(native_preferences_get_scope(c_default)) == "default",
      "default constructors use the documented default scope without mutating it");
    native_preferences_free(c_default);
    if (command == "seed") {
      Check(a.Clear() && b.GetAll().empty() && PreferenceMap(c).empty(), "clear is visible to existing C++ and C instances");
      Check(!a.Remove("missing") && !native_preferences_remove(c, "missing"), "removing an absent key must return false");
      Check(a.Get("missing", "fallback 中文🙂") == "fallback 中文🙂" &&
        PreferenceString(native_preferences_get(c, "missing", "fallback 中文🙂")) == "fallback 中文🙂" && !b.Contains("missing"),
        "missing C++/C reads preserve explicit Unicode defaults");
      Check(a.Set("unicode", "中文🙂") && b.Get("unicode") == "中文🙂", "existing instance sees C++ Unicode publication");
      Check(b.Set("empty", "") && a.Contains("empty") && a.Get("empty", "fallback").empty(), "empty values remain present");
      Check(native_preferences_set(c, "key/路径", "line1\nline2=\\n\t🙂") &&
        a.Get("key/路径") == "line1\nline2=\\n\t🙂", "C publication preserves delimiters and Unicode exactly");
      Check(a.Set("replace", "old") && native_preferences_set(c, "replace", "new") && b.Get("replace") == "new", "replacement is visible through existing instances");
      Check(native_preferences_remove(c, "replace") && !a.Contains("replace") && !b.Remove("replace"), "C deletion and second deletion preserve the contract");
      const auto held = a.GetAll();
      auto keys = native_preferences_get_keys(c);
      std::vector<std::string> c_keys;
      for (long i = 0; i < keys.count; ++i) c_keys.emplace_back(keys.items[i]);
      native_string_list_free(&keys);
      auto expected_keys = a.GetKeys();
      std::sort(c_keys.begin(), c_keys.end()); std::sort(expected_keys.begin(), expected_keys.end());
      Check(c_keys == expected_keys && a.GetSize() == held.size() &&
        native_preferences_get_size(c) == held.size() && PreferenceMap(c) == held,
        "enumeration size and exact keys/maps agree through C and C++");
      auto owned = native_preferences_get_all(c);
      if (owned.count && owned.values[0] && owned.values[0][0]) owned.values[0][0] = 'X';
      native_string_map_free(&owned);
      Check(a.GetAll() == held, "C map allocations are independent of native storage");
      Check(!a.Set(std::string("bad\0key", 7), "bad") &&
        !a.Set("unicode", std::string("bad\0value", 9)) &&
        a.Get(std::string("bad\0key", 7), "fallback") == "fallback" &&
        !a.Contains(std::string("bad\0key", 7)) && !a.Remove(std::string("bad\0key", 7)) &&
        a.GetAll() == held, "embedded NUL inputs fail without truncating or changing prior data");
      // Scope encoding must preserve byte identity and prevent path traversal.
      const std::array<std::string, 4> scopes{scope + "/../隔离", scope + "_2f", scope + "/", scope + std::string("\0tail", 5)};
      for (size_t i = 0; i < scopes.size(); ++i) {
        Preferences isolated(scopes[i]);
        Check(isolated.Clear() && isolated.Set("isolation", std::to_string(i)), "encoded scope remains writable inside the sandbox");
      }
      for (size_t i = 0; i < scopes.size(); ++i) {
        Preferences isolated(scopes[i]);
        Check(isolated.GetScope() == scopes[i] && isolated.Get("isolation") == std::to_string(i) &&
          !a.Contains("isolation") && isolated.Clear(), "scope variants do not collide and clean independently");
      }
    } else if (command == "verify") {
      Check(a.Get("unicode") == "中文🙂" && b.Get("empty", "fallback").empty() && b.Contains("empty"), "native strings survive close and actual process restart");
      Check(a.Get("sdk") == "SDK→native 中文🙂" &&
        PreferenceString(native_preferences_get(c, "sdk", "fallback")) == "SDK→native 中文🙂", "independent ArkTS SDK publication is readable through C++ and C");
      Check(a.Get("key/路径") == "line1\nline2=\\n\t🙂", "persistent delimiters remain exact");
      Check(a.GetAll() == b.GetAll() && a.GetAll() == PreferenceMap(c), "reopened C++/C instances see the same complete persistent map");
    } else if (command == "concurrent") {
      std::atomic<int> ready{0}, removed{0}; std::atomic<bool> go{false};
      Check(a.Set("race-remove", "one"), "seed same-key concurrent removal");
      std::array<std::thread, 8> threads;
      for (int i = 0; i < 8; ++i) threads[i] = std::thread([&, i] {
        Preferences own(scope);
        auto handle = native_preferences_create_with_scope(scope.c_str());
        ++ready; while (!go.load()) std::this_thread::yield();
        if ((i % 2 ? own.Remove("race-remove") : native_preferences_remove(handle, "race-remove"))) ++removed;
        for (int round = 0; round < 6; ++round) {
          const auto key = "worker-" + std::to_string(i) + "-" + std::to_string(round);
          const auto value = "worker value " + std::to_string(round) + "🙂";
          Check(own.Set(key, value) && PreferenceString(native_preferences_get(handle, key.c_str(), "missing")) == value,
            "parallel instances preserve disjoint-key C++/C publication");
          Check(native_preferences_remove(handle, key.c_str()) && !own.Contains(key), "parallel C deletion is visible to its C++ instance");
        }
        native_preferences_free(handle);
      });
      while (ready.load() != 8) std::this_thread::yield();
      go = true; for (auto& thread : threads) thread.join();
      Check(removed.load() == 1 && !a.Contains("race-remove"), "exactly one competing C++/C removal succeeds");
      Check(a.GetAll().size() == 4 && a.GetAll() == PreferenceMap(c), "parallel open/write/remove/close preserves unrelated baseline data");
      // This different scope has no permanently held anchor. Its final owner
      // can close while another thread opens it again.
      const auto reopened_scope = scope + "-reopen";
      std::array<std::thread, 4> reopeners;
      for (int i = 0; i < 4; ++i) reopeners[i] = std::thread([&, i] {
        for (int round = 0; round < 8; ++round) {
          Preferences temporary(reopened_scope);
          const auto key = "owner-" + std::to_string(i);
          Check(temporary.Set(key, "reopened") && temporary.Get(key) == "reopened" && temporary.Remove(key),
            "unanchored concurrent open/write/remove/final-close preserves live handles");
          std::this_thread::yield();
        }
      });
      for (auto& thread : reopeners) thread.join();
      Preferences reopened(reopened_scope);
      Check(reopened.GetAll().empty() && reopened.Clear(), "final-close stress leaves a clean persistent store");
    } else if (command == "limits") {
      const auto before = a.GetAll();
      const std::string key(1024, 'k'), too_long_key(1025, 'k');
      Check(a.Set(key, "boundary") && b.Get(key) == "boundary" &&
        native_preferences_contains(c, key.c_str()), "1024-byte key roundtrips through existing C++/C instances");
      Check(!a.Set(too_long_key, "rejected") && !a.Contains(too_long_key) &&
        a.Get(too_long_key, "missing") == "missing" && !a.Remove(too_long_key), "over-limit keys fail without truncated aliases");
      Check(a.Remove(key), "remove boundary key");
      const std::string large(16 * 1024 * 1024, 'v');
      Check(a.Set("large", large) && b.Get("large") == large &&
        PreferenceString(native_preferences_get(c, "large", "missing")) == large,
        "16 MiB value survives exact C++ and C reads");
      Check(!a.Set("large", large + "x") && a.Get("large") == large, "over-limit value refuses replacement without losing the old value");
      Check(native_preferences_remove(c, "large") && a.GetAll() == before,
        "large-value boundary checks preserve unrelated data and clean up");
    } else if (command == "clear") {
      Check(native_preferences_clear(c) && a.GetAll().empty() && b.GetKeys().empty() && a.GetSize() == 0,
        "C clear is immediately visible through existing instances");
      Check(a.Clear() && !a.Remove("missing"), "empty clear remains successful and missing deletion stays false");
    } else if (command == "empty") {
      Check(a.GetAll().empty() && PreferenceMap(c).empty() && a.GetSize() == 0 &&
        a.Get("unicode", "gone") == "gone", "clear survives real process restart");
    } else Check(false, "unknown preferences command");
    values = a.GetAll();
    native_preferences_free(c);
    Check(!native_preferences_set(c, "invalid", "bad") &&
      !native_preferences_remove(c, "invalid") && !native_preferences_clear(c) &&
      !native_preferences_contains(c, "unicode") && PreferenceMap(c).empty(),
      "released C handle fails safely");
    native_preferences_free(c);
  }
};
inline napi_value PreferencesCommand(napi_env env, napi_callback_info info) {
  napi_value args[2]{}; size_t count = 2;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  if (count != 2) return nullptr;
  auto data = std::make_unique<PreferencesWork>();
  data->command = ohos::js::String(env, args[0]); data->scope = ohos::js::String(env, args[1]);
  if (data->command == "seed" && data->scope.rfind("nativeapi-pref-test-", 0) == 0 && data->scope.size() < 70) {
    data->Check(IsMainThread(), "small preferences UI operations run on the owning main thread");
    const auto scope = data->scope + "-ui";
    Preferences ui(scope);
    auto c = native_preferences_create_with_scope(scope.c_str());
    data->Check(ui.Clear() && native_preferences_set(c, "ui", "UI 中文🙂") && ui.Get("ui") == "UI 中文🙂",
      "UI C publication is readable through C++");
    data->Check(ui.GetKeys() == std::vector<std::string>{"ui"} && ui.GetSize() == 1 && PreferenceMap(c) == ui.GetAll(),
      "UI C++ and C enumeration agrees");
    native_preferences_free(c);
    data->Check(ui.Get("ui") == "UI 中文🙂" && ui.Set("after-close", "live"), "closing a C instance preserves the live C++ store");
    data->Check(ui.Remove("ui") && !ui.Remove("ui") && ui.Clear(), "UI removal and clear preserve the contract");
  }
  napi_value promise = nullptr, resource = nullptr;
  napi_create_promise(env, &data->deferred, &promise);
  napi_create_string_utf8(env, "nativeapi.preferences", NAPI_AUTO_LENGTH, &resource);
  auto status = napi_create_async_work(env, nullptr, resource,
    [](napi_env, void* raw) { static_cast<PreferencesWork*>(raw)->Run(); },
    [](napi_env env, napi_status status, void* raw) {
      std::unique_ptr<PreferencesWork> data(static_cast<PreferencesWork*>(raw));
      data->Check(status == napi_ok && IsMainThread(), "preferences completion returns to UI");
      napi_value object = nullptr, values = nullptr, text = nullptr, global = nullptr, json = nullptr, result = nullptr;
      napi_create_object(env, &object); napi_create_object(env, &values);
      for (const auto& entry : data->values) {
        napi_value key = nullptr;
        napi_create_string_utf8(env, entry.first.data(), entry.first.size(), &key);
        napi_create_string_utf8(env, entry.second.data(), entry.second.size(), &text);
        napi_set_property(env, values, key, text);
      }
      napi_set_named_property(env, object, "values", values);
      for (auto pair : {std::make_pair("passed", data->passed), {"failed", data->failed}})
        napi_set_named_property(env, object, pair.first, ohos::js::Number(env, pair.second));
      napi_get_global(env, &global);
      if (ohos::js::Property(env, global, "JSON", json) && ohos::js::Call(env, json, "stringify", {object}, result))
        napi_resolve_deferred(env, data->deferred, result);
      else napi_reject_deferred(env, data->deferred, object);
      napi_delete_async_work(env, data->work);
    }, data.get(), &data->work);
  if (status != napi_ok) return nullptr;
  if (napi_queue_async_work(env, data->work) != napi_ok) { napi_delete_async_work(env, data->work); return nullptr; }
  data.release(); return promise;
}
}  // namespace nativeapi::tests
