#pragma once

#include <array>

namespace nativeapi::tests {
struct TitleJournal {
  struct Target {
    TitleJournal* owner = nullptr;
    std::shared_ptr<Window> window;
    native_window_t alias = 0;
    size_t object_listener = 0;
    native_listener_id_t c_listener = 0;
    int objects = 0, managers = 0, c_events = 0;
  };
  std::array<Target, 3> targets;
  size_t manager_listener = 0;
  int passed = 0, failed = 0, calls = 0, accepted = 0;
  bool valid = true, last_result = false;

  void Check(bool ok, const char* label) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-title",
                 "%{public}s %{public}s", ok ? "PASS" : "FAIL", label);
  }
  void Bind(size_t index, std::shared_ptr<Window> window) {
    auto& target = targets.at(index);
    target.owner = this;
    target.window = std::move(window);
    if (!target.window) {
      Check(false, "title target attachment failed");
      return;
    }
    target.alias = native_window_manager_get(target.window->GetId());
    target.object_listener = target.window->AddListener<WindowResizedEvent>(
        [&target](const auto& event) {
          ++target.objects;
          const auto size = event.GetNewSize();
          target.owner->valid &= IsMainThread() &&
              event.GetWindowId() == target.window->GetId() &&
              std::isfinite(size.width) && std::isfinite(size.height) &&
              size.width > 0 && size.height > 0;
        });
    target.c_listener = native_window_add_listener(
        target.alias,
        [](const native_window_event_t* event, void* data) {
          auto& t = *static_cast<Target*>(data);
          if (event->type != NATIVE_WINDOW_EVENT_TYPE_RESIZED) return;
          ++t.c_events;
          const auto size = event->data.resized.new_size;
          t.owner->valid &= IsMainThread() && event->window_id == t.window->GetId() &&
              std::isfinite(size.width) && std::isfinite(size.height) &&
              size.width > 0 && size.height > 0;
        }, &target, nullptr);
    Check(target.alias && target.object_listener && target.c_listener,
          "title target C++/C aliases and resize subscriptions installed");
  }
  void Setup() {
    Bind(0, Application::GetInstance().GetPrimaryWindow());
    manager_listener = WindowManager::GetInstance().AddListener<WindowResizedEvent>(
        [this](const auto& event) {
          for (auto& target : targets) {
            if (target.window && target.window->GetId() == event.GetWindowId()) {
              ++target.managers;
              valid &= IsMainThread();
            }
          }
        });
    Check(manager_listener != 0, "manager resize listener installed");
  }
  void Verify() {
    for (auto& target : targets) {
      if (!target.window) { Check(false, "missing title target"); continue; }
      Check(target.window && target.alias &&
                target.window->GetId() == native_window_get_id(target.alias) &&
                target.window->IsMaximized() == native_window_is_maximized(target.alias) &&
                target.window->IsMinimized() == native_window_is_minimized(target.alias) &&
                target.window->IsMaximizable() == native_window_is_maximizable(target.alias),
            "title action C++/C identity and state agree");
      const auto cpp = target.window->GetBounds();
      const auto c = native_window_get_bounds(target.alias);
      Check(std::abs(cpp.x - c.x) < 0.01 && std::abs(cpp.y - c.y) < 0.01 &&
                std::abs(cpp.width - c.width) < 0.01 &&
                std::abs(cpp.height - c.height) < 0.01,
            "title action C++/C bounds agree");
      Check(valid && target.objects == target.managers &&
                target.objects == target.c_events,
            "title action resize paths agree on UI and window identity");
    }
  }
  void Invalid() {
    Window empty, foreign(reinterpret_cast<void*>(uintptr_t{123}));
    Check(!empty.PerformTitleBarDoubleClick() && !foreign.PerformTitleBarDoubleClick() &&
              !native_window_perform_title_bar_double_click(0),
          "empty and foreign title actions are refused");
    const auto stale = native_window_manager_get(targets[0].window->GetId());
    native_window_free(stale);
    Check(!native_window_perform_title_bar_double_click(stale),
          "released C title handle is refused without changing its live window");
  }
  std::string State() const {
    std::ostringstream out;
    out << "{\"passed\":" << passed << ",\"failed\":" << failed
        << ",\"calls\":" << calls << ",\"accepted\":" << accepted
        << ",\"lastResult\":" << (last_result ? "true" : "false") << ",\"windows\":[";
    for (size_t i = 0; i < targets.size(); ++i) {
      if (i) out << ',';
      const auto& target = targets[i];
      if (!target.window) { out << "null"; continue; }
      const auto bounds = target.window->GetBounds();
      out << "{\"id\":" << target.window->GetId()
          << ",\"alive\":" << (target.window->GetNativeObject() ? "true" : "false")
          << ",\"bounds\":{\"x\":" << bounds.x << ",\"y\":" << bounds.y
          << ",\"width\":" << bounds.width << ",\"height\":" << bounds.height << '}'
          << ",\"maximized\":" << (target.window->IsMaximized() ? "true" : "false")
          << ",\"minimized\":" << (target.window->IsMinimized() ? "true" : "false")
          << ",\"maximizable\":" << (target.window->IsMaximizable() ? "true" : "false")
          << ",\"events\":[" << target.objects << ',' << target.managers << ','
          << target.c_events << "]}";
    }
    out << "]}";
    return out.str();
  }
  void Finish() {
    Verify();
    Invalid();
    Check(!targets[1].window->GetNativeObject() && !targets[2].window->GetNativeObject(),
          "retained child wrappers are actually destroyed");
    for (auto& target : targets) {
      Check(target.window->RemoveListener(target.object_listener) &&
                native_window_remove_listener(target.alias, target.c_listener),
            "title target listeners removed");
      native_window_free(target.alias);
      target.alias = 0;
    }
    Check(WindowManager::GetInstance().RemoveListener(manager_listener),
          "title manager listener removed");
    OH_LOG_Print(LOG_APP, failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-title",
                 "NATIVEAPI_TITLE_FINAL_%{public}s passed=%{public}d failed=%{public}d",
                 failed ? "FAIL" : "PASS", passed, failed);
  }
};
inline TitleJournal title_journal;

inline napi_value TitleState(napi_env env, napi_callback_info info) {
  size_t count = 2;
  napi_value args[2]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  const auto command = count ? ohos::js::String(env, args[0]) : std::string{};
  if (command == "setup") title_journal.Setup();
  if (command == "attach-child" || command == "attach-ordinary") {
    const auto parent = title_journal.targets[0].window;
    title_journal.Bind(command == "attach-child" ? 1 : 2,
        ohos::AttachWindow(env, args[1], parent ? parent->GetId() : 0));
  }
  if (command == "verify") title_journal.Verify();
  if (command == "invalid") title_journal.Invalid();
  if (command == "disable") title_journal.targets[0].window->SetMaximizable(false);
  if (command == "enable") title_journal.targets[0].window->SetMaximizable(true);
  if (command == "hide-decor") title_journal.targets[0].window->SetTitleBarStyle(TitleBarStyle::Hidden);
  if (command == "show-decor") title_journal.targets[0].window->SetTitleBarStyle(TitleBarStyle::Normal);
  if (command == "finish") title_journal.Finish();
  return ohos::String(env, title_journal.State());
}

inline napi_value TitleAction(napi_env env, napi_callback_info info) {
  size_t count = 5;
  napi_value args[5]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  struct Work {
    napi_deferred deferred{};
    napi_async_work work{};
    std::shared_ptr<Window> window;
    native_window_t alias = 0;
    bool use_c = false, worker = false, expected = false, overlap = false;
    bool result = false, second = false, thread_valid = false;
    void Execute() {
      thread_valid = IsMainThread() != worker;
      result = use_c ? native_window_perform_title_bar_double_click(alias)
                     : window->PerformTitleBarDoubleClick();
      if (overlap) second = use_c ? window->PerformTitleBarDoubleClick()
                                 : native_window_perform_title_bar_double_click(alias);
    }
    void Complete(napi_env env) {
      ++title_journal.calls;
      title_journal.accepted += result;
      title_journal.last_result = result;
      title_journal.Check(thread_valid && result == expected && (!overlap || !second),
          "title action return, execution thread and shared pending guard match");
      napi_resolve_deferred(env, deferred, ohos::String(env, title_journal.State()));
    }
  };
  auto job = std::make_unique<Work>();
  int32_t index = -1;
  if (count != 5 || napi_get_value_int32(env, args[0], &index) != napi_ok ||
      index < 0 || index >= 3 || !title_journal.targets[index].window) return nullptr;
  const auto& target = title_journal.targets[index];
  job->window = target.window;
  job->alias = target.alias;
  napi_get_value_bool(env, args[1], &job->use_c);
  napi_get_value_bool(env, args[2], &job->worker);
  napi_get_value_bool(env, args[3], &job->expected);
  napi_get_value_bool(env, args[4], &job->overlap);
  napi_value promise = nullptr, name = nullptr;
  if (napi_create_promise(env, &job->deferred, &promise) != napi_ok) return nullptr;
  if (!job->worker) {
    job->Execute();job->Complete(env);return promise;
  }
  napi_create_string_utf8(env, "nativeapi.window.title", NAPI_AUTO_LENGTH, &name);
  if (napi_create_async_work(env, nullptr, name,
      [](napi_env, void* data) { static_cast<Work*>(data)->Execute(); },
      [](napi_env env, napi_status status, void* data) {
        std::unique_ptr<Work> job(static_cast<Work*>(data));
        if (status != napi_ok) job->thread_valid = false;
        job->Complete(env);
        napi_delete_async_work(env, job->work);
      }, job.get(), &job->work) != napi_ok || napi_queue_async_work(env, job->work) != napi_ok) {
    if (job->work) napi_delete_async_work(env, job->work);
    title_journal.Check(false, "title worker submission failed");
    napi_resolve_deferred(env, job->deferred, ohos::String(env, title_journal.State()));
    return promise;
  }
  job.release();
  return promise;
}
}  // namespace nativeapi::tests
