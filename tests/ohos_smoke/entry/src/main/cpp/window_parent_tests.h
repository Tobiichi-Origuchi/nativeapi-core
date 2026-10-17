#pragma once
#include "../../../../../../src/capi/window_c.h"
#include "../../../../../../src/capi/window_manager_c.h"
#include "../../../../../../src/capi/window_shadow_c.h"
#include <array>

namespace nativeapi::tests {
struct ParentJournal {
  std::array<std::shared_ptr<Window>, 4> windows;
  static int System(const std::shared_ptr<Window>& w) {
    auto state = w ? ohos::FindWindowState(w->GetNativeObject()) : nullptr;
    return state ? state->system_id : 0;
  }
  void Setup() {
    windows[0] = Application::GetInstance().GetPrimaryWindow();
    for (int i = 1; i < 4; ++i)
      ohos::CreateSubWindow("NativeAPI Parent " + std::to_string(i), "pages/ControlsTarget",
        [this, i](auto child) {
          windows[i] = child;
          if (child) { child->SetBounds({100.0 + i * 30, 150.0 + i * 30, 240, 180}); child->Show(); }
        });
  }
  std::string Snapshot() {
    std::ostringstream out;
    out << "{\"ready\":" << (windows[0] && windows[1] && windows[2] && windows[3] ? "true" : "false")
        << ",\"system\":[";
    for (int i = 0; i < 4; ++i) { if (i) out << ','; out << System(windows[i]); }
    out << "],\"parents\":[";
    for (int i = 0; i < 4; ++i) { if (i) out << ','; out << (windows[i] ? System(windows[i]->GetParentWindow()) : 0); }
    out << "],\"alive\":[";
    for (int i = 0; i < 4; ++i) { if (i) out << ','; out << (windows[i] && windows[i]->GetNativeObject() ? 1 : 0); }
    out << "]}";
    return out.str();
  }
};
inline ParentJournal parent_journal;
inline napi_value ParentState(napi_env env, napi_callback_info info) {
  size_t count = 1; napi_value value = nullptr;
  napi_get_cb_info(env, info, &count, &value, nullptr, nullptr);
  if (count && ohos::js::String(env, value) == "setup") parent_journal.Setup();
  return ohos::String(env, parent_journal.Snapshot());
}
struct ParentWork {
  napi_async_work work = nullptr; napi_deferred deferred = nullptr;
  int receiver = 3, target = 0, passed = 0, failed = 0;
  bool use_c = false, worker = false, result = false, second = false;
  std::string action;
  std::array<int, 4> parents{};
  void Check(bool ok, const char* text) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-parent", "%{public}s %{public}s", ok ? "PASS" : "FAIL", text);
  }
  void Run() {
    auto self = parent_journal.windows[receiver], next = target < 0 ? nullptr : parent_journal.windows[target];
    auto alias = self ? native_window_manager_get(self->GetId()) : 0;
    auto other = next ? native_window_manager_get(next->GetId()) : 0;
    if (action == "set" || action == "pending") {
      result = use_c ? native_window_set_parent_window(alias, other) : self->SetParentWindow(next);
      if (action == "pending") second = self->SetParentWindow(parent_journal.windows[2]);
    }
    if (action == "invalid") {
      Window empty;
      auto foreign = std::make_shared<Window>(reinterpret_cast<void*>(uintptr_t{123}));
      Check(!empty.GetParentWindow() && !foreign->GetParentWindow(), "empty and foreign wrappers have no parent");
      Check(!empty.SetParentWindow(next) && !foreign->SetParentWindow(next) && !self->SetParentWindow(foreign), "invalid native wrappers reject parent changes");
      Check(!self->SetParentWindow(nullptr) && !self->SetParentWindow(self), "detach and self parenting are refused");
      Check(!parent_journal.windows[0]->SetParentWindow(self), "main windows cannot become children");
      Check(!native_window_set_parent_window(0, other) && !native_window_set_parent_window(alias, 0), "empty C handles reject parent changes");
      auto wrong = native_window_shadow_create();
      Check(wrong && !native_window_set_parent_window(alias, wrong) && !native_window_get_parent_window(wrong), "wrong C handle types are rejected");
      native_window_shadow_free(wrong);
    }
    for (int i = 0; i < 4; ++i) {
      const auto w = parent_journal.windows[i];
      auto c = native_window_manager_get(w->GetId());
      auto p = w->GetParentWindow();
      auto cp = native_window_get_parent_window(c);
      parents[i] = ParentJournal::System(p);
      Check((p ? p->GetId() : 0) == native_window_get_id(cp), "C++ and C parent getters agree");
      Check(!p || WindowManager::GetInstance().Get(p->GetId()) == p, "parent getter preserves canonical shared identity");
      const auto before = p ? p->GetId() : 0;
      native_window_free(cp);
      auto retained = w->GetParentWindow();
      Check((retained ? retained->GetId() : 0) == before, "releasing owning C parent handles preserves native relationship");
      native_window_free(c);
    }
    native_window_free(alias); native_window_free(other);
  }
  std::string Reply() const {
    std::ostringstream out;
    out << "{\"passed\":" << passed << ",\"failed\":" << failed << ",\"result\":" << (result ? "true" : "false")
        << ",\"second\":" << (second ? "true" : "false") << ",\"parents\":[";
    for (int i = 0; i < 4; ++i) { if (i) out << ','; out << parents[i]; }
    out << "]}"; return out.str();
  }
};
inline napi_value ParentCommand(napi_env env, napi_callback_info info) {
  size_t count = 5; napi_value args[5]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  auto job = std::make_unique<ParentWork>();
  if (count != 5) return nullptr;
  job->action = ohos::js::String(env, args[0]);
  napi_get_value_int32(env, args[1], &job->receiver); napi_get_value_int32(env, args[2], &job->target);
  napi_get_value_bool(env, args[3], &job->use_c); napi_get_value_bool(env, args[4], &job->worker);
  if (job->receiver < 0 || job->receiver > 3 || job->target < -1 || job->target > 3 || !parent_journal.windows[3]) return nullptr;
  napi_value promise = nullptr, name = ohos::String(env, "nativeapi.parent");
  if (napi_create_promise(env, &job->deferred, &promise) != napi_ok) return nullptr;
  if (!job->worker) {
    job->Run(); napi_resolve_deferred(env, job->deferred, ohos::String(env, job->Reply())); return promise;
  }
  if (napi_create_async_work(env, nullptr, name,
    [](napi_env, void* data) { static_cast<ParentWork*>(data)->Run(); },
    [](napi_env env, napi_status status, void* data) {
      std::unique_ptr<ParentWork> current(static_cast<ParentWork*>(data));
      if (status == napi_ok) napi_resolve_deferred(env, current->deferred, ohos::String(env, current->Reply()));
      else napi_reject_deferred(env, current->deferred, ohos::String(env, "parent async work failed"));
      napi_delete_async_work(env, current->work);
    }, job.get(), &job->work) != napi_ok || napi_queue_async_work(env, job->work) != napi_ok) {
    if (job->work) napi_delete_async_work(env, job->work);
    napi_reject_deferred(env, job->deferred, ohos::String(env, "parent async work submission failed"));
    return promise;
  }
  job.release(); return promise;
}
}  // namespace nativeapi::tests
