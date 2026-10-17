#pragma once
#include "../../../../../../src/capi/window_manager_c.h"
#include <array>
#include <mutex>
#include <thread>

namespace nativeapi::tests {
struct ManagerJournal {
  std::shared_ptr<Window> main, a, b;
  int passed = 0, failed = 0;
  void Setup() {
    if (main) return;
    main = Application::GetInstance().GetPrimaryWindow();
    for (int index = 0; index < 2; ++index)
      ohos::CreateSubWindow(index ? "NativeAPI Manager B" : "NativeAPI Manager A", "pages/ControlsTarget",
        [this, index](auto child) {
          (index ? b : a) = child;
          if (child) { child->SetBounds({100, 150, 260, 220}); child->Show(); }
        });
  }
  static int System(const std::shared_ptr<Window>& w) {
    auto state = w ? ohos::FindWindowState(w->GetNativeObject()) : nullptr;
    return state ? state->system_id : 0;
  }
  std::string State() {
    std::ostringstream out;
    out << "{\"ready\":" << (main && a && b ? "true" : "false") << ",\"ids\":[";
    for (const auto& w : {main, a, b}) out << (w ? w->GetId() : 0) << ',';
    out << "0],\"system\":[" << System(main) << ',' << System(a) << ',' << System(b) << "],\"alive\":["
        << (main && main->GetNativeObject() ? 1 : 0) << ',' << (a && a->GetNativeObject() ? 1 : 0) << ','
        << (b && b->GetNativeObject() ? 1 : 0) << "]}";
    return out.str();
  }
};
inline ManagerJournal manager_journal;
inline napi_value ManagerState(napi_env env, napi_callback_info info) {
  size_t count = 1; napi_value command = nullptr;
  napi_get_cb_info(env, info, &count, &command, nullptr, nullptr);
  const auto action = count ? ohos::js::String(env, command) : "";
  if (action == "setup") manager_journal.Setup();
  if (action == "close-a" && manager_journal.a) manager_journal.a->Close();
  const auto text = manager_journal.State();
  napi_value value = nullptr; napi_create_string_utf8(env, text.data(), text.size(), &value); return value;
}
struct ManagerProbe {
  napi_async_work work = nullptr; napi_deferred deferred = nullptr;
  Point point{};
  std::array<WindowId, 4> excluded{};
  int passed = 0, failed = 0, samples = 0;
  std::mutex mutex;
  std::string ui, worker, refreshed_ui;
  void Check(bool ok, const char* text) {
    std::lock_guard<std::mutex> lock(mutex); (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-manager", "%{public}s %{public}s", ok ? "PASS" : "FAIL", text);
  }
  std::string Snapshot() {
    auto& manager = WindowManager::GetInstance();
    auto all = manager.GetAll();
    auto c_all = native_window_manager_get_all();
    Check(all.size() == static_cast<size_t>(c_all.count), "C++ and C enumeration size agrees");
    for (size_t i = 0; i < all.size() && i < static_cast<size_t>(c_all.count); ++i)
      Check(manager.Get(all[i]->GetId()) == all[i] && native_window_get_id(c_all.windows[i]) == all[i]->GetId(),
        "C++ identity and owning C enumeration preserve canonical IDs");
    native_window_list_free(&c_all);
    for (const auto& retained : {manager_journal.main, manager_journal.a, manager_journal.b}) {
      if (!retained || retained->GetNativeObject()) continue;
      const auto stale = native_window_manager_get(retained->GetId());
      Check(retained->GetId() != 0 && !manager.Get(retained->GetId()) && !stale,
        "closed retained identity cannot be looked up through C++ or C");
      native_window_free(stale);
    }
    const auto count = all.size(); all.clear();
    Check(manager.GetAll().size() == count, "editing an enumeration snapshot does not mutate the registry");
    Check(!manager.Get(0) && !manager.Get(std::numeric_limits<WindowId>::max()) && !native_window_manager_get(0), "invalid IDs do not produce a window");
    auto current = manager.GetCurrent();
    auto c_current = native_window_manager_get_current();
    Check((current ? current->GetId() : 0) == native_window_get_id(c_current), "C++ and C current-window identity agrees");
    native_window_free(c_current);
    std::ostringstream out;
    out << "{\"count\":" << count << ",\"current\":" << ManagerJournal::System(current) << ",\"hits\":[";
    for (size_t i = 0; i < excluded.size(); ++i) {
      auto hit = manager.GetWindowAtPoint(point, excluded[i]);
      auto c_hit = native_window_manager_get_window_at_point({point.x, point.y}, excluded[i]);
      Check((hit ? hit->GetId() : 0) == native_window_get_id(c_hit), "C++ and C point/exclusion lookup agrees");
      native_window_free(c_hit);
      if (i) out << ','; out << ManagerJournal::System(hit);
    }
    out << "]}";
    Check(!manager.GetWindowAtPoint({std::numeric_limits<double>::quiet_NaN(), point.y}, 0) &&
      !manager.GetWindowAtPoint({point.x, std::numeric_limits<double>::infinity()}, 0), "nonfinite query coordinates safely return no hit");
    return out.str();
  }
};
inline napi_value ManagerQuery(napi_env env, napi_callback_info info) {
  size_t count = 2; napi_value args[2]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  auto data = std::make_unique<ManagerProbe>();
  if (!manager_journal.main || !manager_journal.a || !manager_journal.b || count != 2 || napi_get_value_double(env, args[0], &data->point.x) != napi_ok ||
      napi_get_value_double(env, args[1], &data->point.y) != napi_ok) return nullptr;
  data->excluded = {0, manager_journal.main->GetId(), manager_journal.a->GetId(), manager_journal.b->GetId()};
  data->Check(IsMainThread(), "manager UI queries run on the owning UI thread");
  data->ui = data->Snapshot();
  napi_value promise = nullptr, name = nullptr;
  napi_create_promise(env, &data->deferred, &promise);
  napi_create_string_utf8(env, "nativeapi.manager", NAPI_AUTO_LENGTH, &name);
  auto status = napi_create_async_work(env, nullptr, name,
    [](napi_env, void* raw) {
      auto* data = static_cast<ManagerProbe*>(raw);
      data->Check(!IsMainThread(), "manager async driver is a real worker");
      std::array<std::thread, 4> threads;
      for (size_t i = 0; i < threads.size(); ++i) threads[i] = std::thread([&, i] {
        for (int j = 0; j < 2; ++j) {
          const auto snapshot = data->Snapshot();
          bool coherent = false;
          {
            std::lock_guard<std::mutex> lock(data->mutex);
            if (data->worker.empty()) data->worker = snapshot;
            coherent = data->worker == snapshot;
            ++data->samples;
          }
          data->Check(coherent, "concurrent fresh C++/C manager reads match each other");
        }
      });
      for (auto& thread : threads) thread.join();
    },
    [](napi_env env, napi_status status, void* raw) {
      std::unique_ptr<ManagerProbe> data(static_cast<ManagerProbe*>(raw));
      data->Check(status == napi_ok && IsMainThread(), "manager completion returns to UI without nesting its loop");
      data->refreshed_ui = data->Snapshot();
      data->Check(data->refreshed_ui == data->worker, "UI snapshot matches the completed fresh worker stack");
      std::ostringstream out; out << "{\"passed\":" << data->passed << ",\"failed\":" << data->failed << ",\"samples\":" << data->samples
        << ",\"ui\":" << data->ui << ",\"worker\":" << data->worker << ",\"refreshedUi\":" << data->refreshed_ui << '}';
      const auto text = out.str(); napi_value result = nullptr;
      napi_create_string_utf8(env, text.data(), text.size(), &result);
      napi_resolve_deferred(env, data->deferred, result); napi_delete_async_work(env, data->work);
    }, data.get(), &data->work);
  if (status != napi_ok) return nullptr;
  if (napi_queue_async_work(env, data->work) != napi_ok) { napi_delete_async_work(env, data->work); return nullptr; }
  data.release(); return promise;
}
}  // namespace nativeapi::tests
