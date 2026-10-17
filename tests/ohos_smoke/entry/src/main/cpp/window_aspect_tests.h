#pragma once
namespace nativeapi::tests {
struct AspectJournal {
  std::shared_ptr<Window> main, child, borrowed;
  native_window_t alias = 0, child_alias = 0;
  int passed = 0, failed = 0;
  void Check(bool ok, const char* message) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-aspect", "%{public}s %{public}s",
                 ok ? "PASS" : "FAIL", message);
  }
  void Setup() {
    Window invalid(reinterpret_cast<void*>(uintptr_t{123}));
    invalid.SetAspectRatio(1.5);
    Window{}.SetAspectRatio(1);
    native_window_set_aspect_ratio(0, 2);
    Check(invalid.GetAspectRatio() == 0 && native_window_get_aspect_ratio(0) == 0,
          "foreign/default C++ and empty C aspect requests are safe");
    main = Application::GetInstance().GetPrimaryWindow();
    alias = main ? native_window_manager_get(main->GetId()) : 0;
    borrowed = main ? std::make_shared<Window>(main->GetNativeObject()) : nullptr;
    Check(main && alias && borrowed && borrowed->GetId() == main->GetId(),
          "C++ borrowed and C aliases share the primary native window");
    Check(ohos::CreateSubWindow("NativeAPI Aspect Child", "pages/ControlsTarget",
                                [this](auto w) {
                                  child = w;
                                  Check(bool(w), "real native aspect child created");
                                  if (w) {
                                    child_alias = native_window_manager_get(w->GetId());
                                    w->SetBounds({70, 130, 120, 70});
                                    w->Show();
                                  }
                                }),
          "aspect child creation submitted");
  }
  std::string State() {
    auto state = main ? ohos::FindWindowState(main->GetNativeObject()) : nullptr;
    std::ostringstream s;
    s << "{\"ready\":" << (child ? "true" : "false")
      << ",\"alive\":" << (child && child->GetNativeObject() ? "true" : "false")
      << ",\"ratio\":" << (main ? main->GetAspectRatio() : 0)
      << ",\"pending\":" << (state ? state->aspect_requests.size() : 0) << ",\"passed\":" << passed
      << ",\"failed\":" << failed << "}";
    return s.str();
  }
  void Verify() {
    Check(main && borrowed && main->GetAspectRatio() == borrowed->GetAspectRatio() &&
              main->GetAspectRatio() == native_window_get_aspect_ratio(alias),
          "C++/borrowed/C aliases share the successful aspect constraint");
    Check(child && child->GetAspectRatio() == 0 && native_window_get_aspect_ratio(child_alias) == 0,
          "unsupported ordinary child never invents an aspect constraint");
  }
  bool Call(const std::string& command, bool use_c) {
    if (!main || !child)
      return false;
    auto set = [&](double value) {
      if (use_c)
        native_window_set_aspect_ratio(alias, value);
      else
        borrowed->SetAspectRatio(value);
    };
    if (command == "invalid") {
      const auto before = main->GetAspectRatio();
      set(std::numeric_limits<double>::quiet_NaN());
      set(std::numeric_limits<double>::infinity());
      set(-std::numeric_limits<double>::infinity());
      return main->GetAspectRatio() == before;
    }
    if (command == "queued") {
      set(1);
      set(2);
      set(1.25);
      return true;
    }
    if (command == "child") {
      child->SetAspectRatio(1.5);
      native_window_set_aspect_ratio(child_alias, 2);
      return child->GetAspectRatio() == 0;
    }
    if (command == "close") {
      child->SetAspectRatio(1.5);
      native_window_set_aspect_ratio(child_alias, 2);
      child->SetAspectRatio(1.25);
      child->Close();
      return true;
    }
    if (command == "programmatic") {
      main->SetContentSize({620, 380});
      return true;
    }
    if (command == "hide-decor" || command == "show-decor") {
      main->SetTitleBarStyle(command == "hide-decor" ? TitleBarStyle::Hidden
                                                     : TitleBarStyle::Normal);
      return true;
    }
    set(command == "square"            ? 1
        : command == "wide"            ? 1.5
        : command == "restore-library" ? 1.25
        : command == "reject"          ? 1e30
        : command == "negative"        ? -5
                                       : 0);
    return true;
  }
};
inline AspectJournal aspect;
inline napi_value AspectState(napi_env env, napi_callback_info info) {
  size_t count = 1;
  napi_value value = nullptr;
  napi_get_cb_info(env, info, &count, &value, nullptr, nullptr);
  const auto command = count ? ohos::js::String(env, value) : std::string();
  if (command == "setup")
    aspect.Setup();
  if (command == "verify")
    aspect.Verify();
  if (command == "finish") {
    aspect.child->SetAspectRatio(1);
    native_window_set_aspect_ratio(aspect.child_alias, 1.5);
    aspect.Check(!aspect.child->GetNativeObject() && aspect.child->GetAspectRatio() == 0 &&
                     native_window_get_aspect_ratio(aspect.child_alias) == 0,
                 "destroyed retained windows safely ignore queued and new aspect requests");
    native_window_free(aspect.alias);
    native_window_free(aspect.child_alias);
    aspect.alias = aspect.child_alias = 0;
    OH_LOG_Print(LOG_APP, aspect.failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-aspect",
                 "NATIVEAPI_ASPECT_FINAL_%{public}s passed=%{public}d failed=%{public}d",
                 aspect.failed ? "FAIL" : "PASS", aspect.passed, aspect.failed);
  }
  return ohos::String(env, aspect.State());
}
inline napi_value AspectAction(napi_env env, napi_callback_info info) {
  size_t count = 3;
  napi_value args[3]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  struct Work {
    napi_deferred deferred{};
    napi_async_work work{};
    std::string command;
    bool c = false, worker = false, result = false;
  };
  auto job = std::make_unique<Work>();
  job->command = ohos::js::String(env, args[0]);
  napi_get_value_bool(env, args[1], &job->c);
  napi_get_value_bool(env, args[2], &job->worker);
  napi_value promise = nullptr, name = nullptr;
  if (napi_create_promise(env, &job->deferred, &promise) != napi_ok)
    return nullptr;
  if (!job->worker) {
    aspect.Check(aspect.Call(job->command, job->c), "UI aspect request submitted");
    napi_resolve_deferred(env, job->deferred, ohos::String(env, aspect.State()));
    return promise;
  }
  napi_create_string_utf8(env, "nativeapi.window.aspect", NAPI_AUTO_LENGTH, &name);
  if (napi_create_async_work(
          env, nullptr, name,
          [](napi_env, void* p) {
            auto* j = static_cast<Work*>(p);
            j->result = aspect.Call(j->command, j->c);
          },
          [](napi_env env, napi_status status, void* p) {
            std::unique_ptr<Work> j(static_cast<Work*>(p));
            aspect.Check(status == napi_ok && j->result,
                         "worker aspect operation completes through the SDK");
            napi_resolve_deferred(env, j->deferred, ohos::String(env, aspect.State()));
            napi_delete_async_work(env, j->work);
          },
          job.get(), &job->work) != napi_ok ||
      napi_queue_async_work(env, job->work) != napi_ok) {
    if (job->work)
      napi_delete_async_work(env, job->work);
    aspect.Check(false, "worker aspect submission failed");
    napi_resolve_deferred(env, job->deferred, ohos::String(env, aspect.State()));
    return promise;
  }
  job.release();
  return promise;
}
}  // namespace nativeapi::tests
