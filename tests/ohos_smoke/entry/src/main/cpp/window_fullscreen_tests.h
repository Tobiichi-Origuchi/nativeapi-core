#pragma once

namespace nativeapi::tests {
struct FullscreenJournal {
  TitleJournal base;
  struct Events {
    size_t object_listener = 0;
    native_listener_id_t c_listener = 0;
    std::array<int, 2> object{}, manager{}, c{};
    WindowId id = 0;
    bool valid = true;
  };
  std::array<Events, 3> events;
  size_t manager_listener = 0;
  bool valid = true;
  int calls = 0;
  static int Kind(const WindowEvent& event) {
    return dynamic_cast<const WindowEnteredFullScreenEvent*>(&event) ? 0 :
           dynamic_cast<const WindowExitedFullScreenEvent*>(&event) ? 1 : -1;
  }
  void Watch() {
    for (size_t i = 0; i < events.size(); ++i) {
      auto& target = base.targets[i];
      events[i].id = target.window->GetId();
      events[i].object_listener = target.window->AddListener<WindowEvent>(
          [this, i](const WindowEvent& event) {
            const auto kind = Kind(event);
            if (kind < 0) return;
            valid &= IsMainThread() && event.GetWindowId() == base.targets[i].window->GetId();
            ++events[i].object[kind];
          });
      events[i].c_listener = native_window_add_listener(target.alias,
          [](const native_window_event_t* event, void* data) {
            auto& e = *static_cast<Events*>(data);
            const int kind = event->type == NATIVE_WINDOW_EVENT_TYPE_ENTERED_FULL_SCREEN ? 0 :
                             event->type == NATIVE_WINDOW_EVENT_TYPE_EXITED_FULL_SCREEN ? 1 : -1;
            if (kind >= 0) { e.valid &= IsMainThread() && event->window_id == e.id; ++e.c[kind]; }
          }, &events[i], nullptr);
      base.Check(events[i].object_listener && events[i].c_listener, "fullscreen subscriptions installed");
    }
    manager_listener = WindowManager::GetInstance().AddListener<WindowEvent>(
        [this](const WindowEvent& event) {
          const auto kind = Kind(event);
          if (kind < 0) return;
          valid &= IsMainThread();
          for (size_t i = 0; i < events.size(); ++i)
            if (event.GetWindowId() == base.targets[i].window->GetId()) ++events[i].manager[kind];
        });
    base.Check(manager_listener != 0, "fullscreen manager subscription installed");
  }
  void Verify() {
    base.Verify();
    for (size_t i = 0; i < events.size(); ++i) {
      const auto& e = events[i]; const auto& t = base.targets[i];
      base.Check(valid && e.valid && e.object == e.manager && e.object == e.c,
                 "actual fullscreen object/manager/C event paths agree");
      base.Check(t.window->IsFullScreen() == native_window_is_full_screen(t.alias),
                 "fullscreen C++/C queries agree");
    }
  }
  std::string State() {
    std::ostringstream out;
    out << "{\"native\":" << base.State() << ",\"calls\":" << calls << ",\"fullscreen\":[";
    for (size_t i = 0; i < events.size(); ++i) {
      if (i) out << ',';
      out << (base.targets[i].window->IsFullScreen() ? "true" : "false");
    }
    out << "],\"events\":[";
    for (size_t i = 0; i < events.size(); ++i) {
      if (i) out << ',';
      const auto& e = events[i];
      out << "[[" << e.object[0] << ',' << e.object[1] << "],[" << e.manager[0] << ','
          << e.manager[1] << "],[" << e.c[0] << ',' << e.c[1] << "]]";
    }
    out << "]}";
    return out.str();
  }
  void Finish() {
    Verify();
    for (size_t i = 0; i < events.size(); ++i) {
      const auto& t = base.targets[i];
      base.Check(t.window->RemoveListener(events[i].object_listener) &&
                     native_window_remove_listener(t.alias, events[i].c_listener),
                 "fullscreen target subscriptions removed");
    }
    base.Check(WindowManager::GetInstance().RemoveListener(manager_listener),
               "fullscreen manager subscription removed");
    base.Finish();
    OH_LOG_Print(LOG_APP, base.failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-fullscreen",
                 "NATIVEAPI_FULLSCREEN_FINAL_%{public}s", base.failed ? "FAIL" : "PASS");
  }
};
inline FullscreenJournal fullscreen;
inline napi_value FullscreenState(napi_env env, napi_callback_info info) {
  napi_value args[2]{}; size_t count = 2;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  const auto command = count ? ohos::js::String(env, args[0]) : std::string{};
  if (command == "setup") { fullscreen.base.Setup(); return ohos::String(env, "{}"); }
  if (command == "attach-child" || command == "attach-ordinary") {
    fullscreen.base.Bind(command == "attach-child" ? 1 : 2,
        ohos::AttachWindow(env, args[1], fullscreen.base.targets[0].window->GetId()));
    return ohos::String(env, "{}");
  }
  if (command == "watch") fullscreen.Watch();
  if (command == "verify") fullscreen.Verify();
  if (command == "invalid") {
    Window empty, foreign(reinterpret_cast<void*>(uintptr_t{123}));
    empty.SetFullScreen(true); foreign.SetFullScreen(true); native_window_set_full_screen(0, true);
    const auto stale = native_window_manager_get(fullscreen.base.targets[0].window->GetId());
    native_window_free(stale); native_window_set_full_screen(stale, true);
    fullscreen.base.Check(!native_window_is_full_screen(stale), "released C fullscreen handle remains inert");
    fullscreen.base.Check(!empty.IsFullScreen() && !foreign.IsFullScreen() && !native_window_is_full_screen(0),
                           "invalid fullscreen wrappers remain inert");
  }
  if (command == "finish") fullscreen.Finish();
  return ohos::String(env, fullscreen.State());
}
inline napi_value FullscreenAction(napi_env env, napi_callback_info info) {
  napi_value args[4]{}; size_t count = 4;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  struct Work {
    std::shared_ptr<Window> window;
    native_window_t alias = 0;
    bool enter = false, use_c = false, worker = false, correct_thread = false;
    napi_deferred deferred{}; napi_async_work work{};
    void Execute() {
      correct_thread = IsMainThread() != worker;
      if (use_c) native_window_set_full_screen(alias, enter);
      else window->SetFullScreen(enter);
    }
    void Complete(napi_env env) {
      ++fullscreen.calls;
      fullscreen.base.Check(correct_thread, "fullscreen mutation uses requested execution thread");
      napi_resolve_deferred(env, deferred, ohos::String(env, "submitted"));
    }
  };
  int32_t index = -1;
  if (count != 4 || napi_get_value_int32(env, args[0], &index) != napi_ok || index < 0 || index >= 3)
    return nullptr;
  auto job = std::make_unique<Work>();
  job->window = fullscreen.base.targets[index].window; job->alias = fullscreen.base.targets[index].alias;
  napi_get_value_bool(env, args[1], &job->enter); napi_get_value_bool(env, args[2], &job->use_c);
  napi_get_value_bool(env, args[3], &job->worker);
  napi_value promise = nullptr, name = nullptr;
  if (napi_create_promise(env, &job->deferred, &promise) != napi_ok) return nullptr;
  if (!job->worker) { job->Execute(); job->Complete(env); return promise; }
  napi_create_string_utf8(env, "nativeapi.fullscreen", NAPI_AUTO_LENGTH, &name);
  if (napi_create_async_work(env, nullptr, name,
      [](napi_env, void* data) { static_cast<Work*>(data)->Execute(); },
      [](napi_env env, napi_status status, void* data) {
        std::unique_ptr<Work> job(static_cast<Work*>(data));
        job->correct_thread &= status == napi_ok; job->Complete(env); napi_delete_async_work(env, job->work);
      }, job.get(), &job->work) != napi_ok || napi_queue_async_work(env, job->work) != napi_ok) {
    if (job->work) napi_delete_async_work(env, job->work);
    fullscreen.base.Check(false, "fullscreen worker submission failed");
    napi_resolve_deferred(env, job->deferred, ohos::String(env, "failed")); return promise;
  }
  job.release(); return promise;
}
}  // namespace nativeapi::tests
