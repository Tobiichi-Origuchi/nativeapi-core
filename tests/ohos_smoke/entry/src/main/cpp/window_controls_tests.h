#pragma once
#include <array>
namespace nativeapi::tests {
struct ControlsJournal {
  std::shared_ptr<Window> main, child;
  native_window_t alias = 0, child_alias = 0;
  size_t object_listener = 0, manager_listener = 0;
  native_listener_id_t c_listener = 0;
  int passed = 0, failed = 0, objects = 0, managers = 0, c_events = 0;
  std::array<int,3> policy_objects{}, policy_managers{}, policy_c{};
  static int PolicyIndex(WindowProperty p) {
    return p == WindowProperty::Minimizable ? 0 : p == WindowProperty::Maximizable ? 1 : p == WindowProperty::Closable ? 2 : -1;
  }
  bool valid = true;
  void Check(bool ok, const char* message) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-controls",
                 "%{public}s %{public}s", ok ? "PASS" : "FAIL", message);
  }
  void Listen() {
    if (object_listener)
      return;
    object_listener = main->AddListener<WindowPropertyChangedEvent>([this](const auto& e) {
      const int index = PolicyIndex(e.GetProperty());
      if (index >= 0) { ++policy_objects[index]; valid &= IsMainThread() && e.GetWindowId() == main->GetId(); }
      if (e.GetProperty() == WindowProperty::WindowControlButtonsVisible) {
        ++objects;
        OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-controls",
                     "NATIVEAPI_CONTROLS_EVENT %{public}d visible=%{public}d", objects,
                     main->IsWindowControlButtonsVisible());
        valid &= IsMainThread() && e.GetWindowId() == main->GetId();
      }
    });
    manager_listener =
        WindowManager::GetInstance().AddListener<WindowPropertyChangedEvent>([this](const auto& e) {
          const int index = PolicyIndex(e.GetProperty());
          if (e.GetWindowId() == main->GetId() && index >= 0) ++policy_managers[index];
          if (e.GetWindowId() == main->GetId() &&
              e.GetProperty() == WindowProperty::WindowControlButtonsVisible)
            ++managers;
        });
    c_listener = native_window_add_listener(
        alias,
        [](const native_window_event_t* e, void* p) {
          auto& j = *static_cast<ControlsJournal*>(p);
          if (e->type == NATIVE_WINDOW_EVENT_TYPE_PROPERTY_CHANGED) {
            const auto p = e->data.property_changed.property;
            const int index = p == NATIVE_WINDOW_PROPERTY_MINIMIZABLE ? 0 : p == NATIVE_WINDOW_PROPERTY_MAXIMIZABLE ? 1 : p == NATIVE_WINDOW_PROPERTY_CLOSABLE ? 2 : -1;
            if (index >= 0) { ++j.policy_c[index]; j.valid &= IsMainThread() && e->window_id == j.main->GetId(); }
          }
          if (e->type == NATIVE_WINDOW_EVENT_TYPE_PROPERTY_CHANGED &&
              e->data.property_changed.property ==
                  NATIVE_WINDOW_PROPERTY_WINDOW_CONTROL_BUTTONS_VISIBLE) {
            ++j.c_events;
            j.valid &= IsMainThread() && e->window_id == j.main->GetId();
          }
        },
        this, nullptr);
    Check(object_listener && manager_listener && c_listener,
          "three public control-property observation paths installed");
  }
  void Unlisten() {
    Check(main->RemoveListener(object_listener) &&
              WindowManager::GetInstance().RemoveListener(manager_listener) &&
              native_window_remove_listener(alias, c_listener),
          "all control-property observation paths removed");
    object_listener = manager_listener = c_listener = 0;
  }
  void Setup() {
    Window invalid(reinterpret_cast<void*>(uintptr_t{123}));
    invalid.SetWindowControlButtonsVisible(true);
    Window{}.SetWindowControlButtonsVisible(false);
    native_window_set_window_control_buttons_visible(0, true);
    Check(!invalid.IsWindowControlButtonsVisible() &&
              !native_window_is_window_control_buttons_visible(0),
          "foreign/default C++ and empty C handles are safe and hidden");
    main = Application::GetInstance().GetPrimaryWindow();
    alias = main ? native_window_manager_get(main->GetId()) : 0;
    Check(main && alias && main->GetId() == native_window_get_id(alias),
          "C++/C aliases address the real primary window");
    Listen();
    Check(ohos::CreateSubWindow("NativeAPI Controls Child", "pages/ControlsTarget",
                                [this](auto w) {
                                  child = w;
                                  Check(bool(w), "real native controls child created");
                                  if (!w)
                                    return;
                                  child_alias = native_window_manager_get(w->GetId());
                                  auto b = main->GetBounds();
                                  w->SetBounds({b.x + 80, b.y + 160, 180, 110});
                                  w->SetHasShadow(false);
                                  w->Show();
                                  Check(child_alias && !w->IsWindowControlButtonsVisible(),
                                        "ordinary undecorated child has no native controls");
                                }),
          "native controls child creation submitted");
  }
  std::string State() {
    std::ostringstream s;
    s << "{\"ready\":" << (child ? "true" : "false")
      << ",\"alive\":" << (child && child->GetNativeObject() ? "true" : "false")
      << ",\"visible\":" << (main && main->IsWindowControlButtonsVisible() ? "true" : "false")
      << ",\"objects\":" << objects << ",\"managers\":" << managers << ",\"cEvents\":" << c_events
      << ",\"passed\":" << passed << ",\"failed\":" << failed;
    auto policies = [&](const std::shared_ptr<Window>& window) {
      s << '[' << (window && window->IsMinimizable() ? "true" : "false") << ','
        << (window && window->IsMaximizable() ? "true" : "false") << ','
        << (window && window->IsClosable() ? "true" : "false") << ']';
    };
    s << ",\"policy\":"; policies(main);
    s << ",\"childPolicy\":"; policies(child);
    auto counts = [&](const std::array<int,3>& values) { s << '[' << values[0] << ',' << values[1] << ',' << values[2] << ']'; };
    s << ",\"policyObjects\":"; counts(policy_objects);
    s << ",\"policyManagers\":"; counts(policy_managers);
    s << ",\"policyC\":"; counts(policy_c);
    s << '}';
    return s.str();
  }
  bool Call(const std::string& command, bool use_c) {
    if (!main || !child)
      return false;
    if (command == "main-close-refused") {
      // Primary Close remains explicitly unsupported by the existing API.
      return !(use_c ? native_window_close(alias) : main->Close());
    }
    if (command == "program-minimize") { main->Minimize(); return true; }
    if (command == "program-restore") { main->Restore(); return true; }
    auto set = [&](const std::shared_ptr<Window>& w, native_window_t c, int index, bool value) {
      if (use_c) {
        if (index == 0) native_window_set_minimizable(c, value);
        else if (index == 1) native_window_set_maximizable(c, value);
        else native_window_set_closable(c, value);
      } else {
        if (index == 0) w->SetMinimizable(value);
        else if (index == 1) w->SetMaximizable(value);
        else w->SetClosable(value);
      }
    };
    if (command == "policy-child") {
      for (bool value : {false,true}) for (int i = 0; i < 3; ++i) set(child,child_alias,i,value);
      return true;
    }
    for (int i = 0; i < 3; ++i) {
      const auto prefix = i == 0 ? "min-" : i == 1 ? "max-" : "close-";
      if (command == std::string(prefix)+"off" || command == std::string(prefix)+"on") {
        set(main,alias,i,command == std::string(prefix)+"on"); return true;
      }
    }
    if (command == "close") {
      child->Close();
      return true;
    }
    if (command == "maximize") {
      main->Maximize();
      return true;
    }
    if (command == "recover") {
      main->Unmaximize();
      return true;
    }
    if (command == "hide-decor" || command == "show-decor") {
      main->SetTitleBarStyle(command == "show-decor" ? TitleBarStyle::Normal
                                                     : TitleBarStyle::Hidden);
      return true;
    }
    if (command == "child") {
      child->SetWindowControlButtonsVisible(true);
      native_window_set_window_control_buttons_visible(child_alias, false);
      return !child->IsWindowControlButtonsVisible() &&
             !native_window_is_window_control_buttons_visible(child_alias);
    }
    const bool visible = command == "show";
    if (use_c)
      native_window_set_window_control_buttons_visible(alias, visible);
    else
      main->SetWindowControlButtonsVisible(visible);
    return true;  // Void API: real SDK/layout/query/observer checks follow in the page and runner.
  }
  void Verify() {
    Check(main && main->IsWindowControlButtonsVisible() ==
                      native_window_is_window_control_buttons_visible(alias),
          "C++ and C read the same live native control layout");
    Check(valid && objects == managers && objects == c_events,
          "object/manager/C control-property events agree on identity and UI thread");
  }
  void VerifyPolicy() {
    Check(main && main->IsMinimizable() == native_window_is_minimizable(alias) &&
              main->IsMaximizable() == native_window_is_maximizable(alias) &&
              main->IsClosable() == native_window_is_closable(alias),
          "three C++/C caption policies agree");
    Check(child && child->IsMinimizable() == native_window_is_minimizable(child_alias) &&
              child->IsMaximizable() == native_window_is_maximizable(child_alias) &&
              child->IsClosable() == native_window_is_closable(child_alias),
          "child C++/C policy aliases agree");
    Check(valid && policy_objects == policy_managers && policy_objects == policy_c,
          "three caption property event paths agree on UI and identity");
  }
};
inline ControlsJournal controls;
inline napi_value ControlsState(napi_env env, napi_callback_info info) {
  size_t count = 1;
  napi_value value = nullptr;
  napi_get_cb_info(env, info, &count, &value, nullptr, nullptr);
  auto command = count ? ohos::js::String(env, value) : std::string();
  if (command == "setup")
    controls.Setup();
  if (command == "verify")
    controls.Verify();
  if (command == "policy-verify") controls.VerifyPolicy();
  if (command == "policy-finish") {
    controls.VerifyPolicy();
    controls.Check(!controls.child->GetNativeObject() && !controls.child->IsMinimizable() &&
        !controls.child->IsMaximizable() && !controls.child->IsClosable(), "closed child caption policies are false");
    Window empty;
    empty.SetMinimizable(true);empty.SetMaximizable(true);empty.SetClosable(true);
    controls.Check(!empty.IsMinimizable() && !empty.IsMaximizable() && !empty.IsClosable() &&
        !native_window_is_minimizable(0) && !native_window_is_maximizable(0) && !native_window_is_closable(0),
        "empty caption policies remain false");
    controls.Unlisten();
    native_window_free(controls.alias);native_window_free(controls.child_alias);
    controls.alias = controls.child_alias = 0;
    OH_LOG_Print(LOG_APP, controls.failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-controls",
      "NATIVEAPI_POLICY_FINAL_%{public}s passed=%{public}d failed=%{public}d", controls.failed ? "FAIL" : "PASS",controls.passed,controls.failed);
  }
  if (command == "unlisten")
    controls.Unlisten();
  if (command == "listen")
    controls.Listen();
  if (command == "finish") {
    controls.child->SetWindowControlButtonsVisible(true);
    native_window_set_window_control_buttons_visible(controls.child_alias, true);
    controls.Check(!controls.child->GetNativeObject() &&
                       !controls.child->IsWindowControlButtonsVisible() &&
                       !native_window_is_window_control_buttons_visible(controls.child_alias),
                   "closed retained wrappers stay hidden and ignore visibility mutations");
    controls.Unlisten();
    native_window_free(controls.alias);
    native_window_free(controls.child_alias);
    controls.alias = controls.child_alias = 0;
    OH_LOG_Print(LOG_APP, controls.failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-controls",
                 "NATIVEAPI_CONTROLS_FINAL_%{public}s passed=%{public}d failed=%{public}d",
                 controls.failed ? "FAIL" : "PASS", controls.passed, controls.failed);
  }
  return ohos::String(env, controls.State());
}
inline napi_value ControlsAction(napi_env env, napi_callback_info info) {
  size_t count = 3;
  napi_value args[3]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  struct Work {
    napi_deferred deferred{};
    napi_async_work work{};
    std::string command;
    bool c = false, worker = false, result = false, thread_valid = false;
  };
  auto job = std::make_unique<Work>();
  job->command = ohos::js::String(env, args[0]);
  napi_get_value_bool(env, args[1], &job->c);
  napi_get_value_bool(env, args[2], &job->worker);
  napi_value promise = nullptr, name = nullptr;
  if (napi_create_promise(env, &job->deferred, &promise) != napi_ok)
    return nullptr;
  if (!job->worker) {
    controls.Check(IsMainThread() && controls.Call(job->command, job->c),
                   "UI control request returns; actual SDK/layout is verified independently");
    napi_resolve_deferred(env, job->deferred, ohos::String(env, controls.State()));
    return promise;
  }
  napi_create_string_utf8(env, "nativeapi.window.controls", NAPI_AUTO_LENGTH, &name);
  if (napi_create_async_work(
          env, nullptr, name,
          [](napi_env, void* p) {
            auto* j = static_cast<Work*>(p);
            j->thread_valid = !IsMainThread();
            j->result = controls.Call(j->command, j->c);
          },
          [](napi_env env, napi_status status, void* p) {
            std::unique_ptr<Work> j(static_cast<Work*>(p));
            controls.Check(status == napi_ok && j->result && j->thread_valid,
                           "worker control request reaches the owning UI; actual layout is "
                           "verified afterwards");
            napi_resolve_deferred(env, j->deferred, ohos::String(env, controls.State()));
            napi_delete_async_work(env, j->work);
          },
          job.get(), &job->work) != napi_ok ||
      napi_queue_async_work(env, job->work) != napi_ok) {
    if (job->work)
      napi_delete_async_work(env, job->work);
    controls.Check(false, "worker control submission failed");
    napi_resolve_deferred(env, job->deferred, ohos::String(env, controls.State()));
    return promise;
  }
  job.release();
  return promise;
}
}  // namespace nativeapi::tests
