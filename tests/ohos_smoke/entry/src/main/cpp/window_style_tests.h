#pragma once
#include "../../../../../../src/capi/window_c.h"
#include "../../../../../../src/capi/window_manager_c.h"

namespace nativeapi::tests {
inline const bool style_before_host =
    Window::IsCornerPreferenceSupported() || native_window_is_corner_preference_supported();
struct StyleJournal {
  std::shared_ptr<Window> target, main;
  native_window_t alias = 0, main_alias = 0;
  int passed = 0, failed = 0;
  void Check(bool ok, const char* message) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-style", "%{public}s %{public}s",
                 ok ? "PASS" : "FAIL", message);
  }
  std::string State() {
    std::ostringstream s;
    s << "{\"ready\":" << (target ? "true" : "false")
      << ",\"alive\":" << (target && target->GetNativeObject() ? "true" : "false")
      << ",\"preference\":" << (target ? int(target->GetCornerPreference()) : -1)
      << ",\"shadow\":" << (target && target->HasShadow() ? "true" : "false")
      << ",\"mainShadow\":" << (main && main->HasShadow() ? "true" : "false")
      << ",\"passed\":" << passed << ",\"failed\":" << failed << "}";
    return s.str();
  }
  void Setup() {
    Check(!style_before_host, "corner capability is false before host initialization");
    Check(Window::IsCornerPreferenceSupported() && native_window_is_corner_preference_supported(),
          "C++ and C report initialized corner support");
    Window invalid(reinterpret_cast<void*>(uintptr_t{123}));
    Check(!invalid.SetCornerPreference(WindowCornerPreference::Round) &&
              invalid.GetCornerPreference() == WindowCornerPreference::Default &&
              !invalid.HasShadow(),
          "foreign native tokens remain safe and unconfigured");
    Check(!native_window_set_corner_preference(0, NATIVE_WINDOW_CORNER_PREFERENCE_ROUND) &&
              native_window_get_corner_preference(0) == NATIVE_WINDOW_CORNER_PREFERENCE_DEFAULT,
          "empty C handles reject corner mutations");
    main = Application::GetInstance().GetPrimaryWindow();
    main_alias = main ? native_window_manager_get(main->GetId()) : 0;
    Check(main && !main->SetCornerPreference(WindowCornerPreference::Round),
          "main window corner mutation is rejected");
    Check(ohos::CreateSubWindow(
              "NativeAPI Window Style", "pages/StyleTarget",
              [this](auto w) {
                target = w;
                Check(bool(target), "real native style child created");
                if (!target)
                  return;
                alias = native_window_manager_get(target->GetId());
                auto b = main->GetBounds();
                target->SetBounds({b.x + 80, b.y + 100, 180, 160});
                target->SetBackgroundColor(Color::FromRGBA(255, 0, 255));
                target->SetHasShadow(false);
                target->Show();
                Check(alias && native_window_get_id(alias) == target->GetId(),
                      "C alias shares the same native window identity");
                Check(!target->SetCornerPreference(static_cast<WindowCornerPreference>(99)) &&
                          !native_window_set_corner_preference(
                              alias, static_cast<native_window_corner_preference_t>(99)),
                      "invalid corner enums fail without changing a native preference");
              }),
          "native child creation submitted");
  }
  bool Call(const std::string& command, bool use_c) {
    if (!target)
      return false;
    if (command == "main-shadow-off" || command == "main-shadow-on") {
      const bool value = command == "main-shadow-on";
      if (use_c)
        native_window_set_has_shadow(main_alias, value);
      else
        main->SetHasShadow(value);
      return true;  // Void main-window setter completes asynchronously in the SDK.
    }
    if (command == "pending") {
      const auto prior = target->GetCornerPreference();
      const bool first = target->SetCornerPreference(WindowCornerPreference::DoNotRound);
      const bool second =
          native_window_set_corner_preference(alias, NATIVE_WINDOW_CORNER_PREFERENCE_ROUND);
      return first && !second && target->GetCornerPreference() == prior;
    }
    if (command == "close") {
      target->Close();
      return true;
    }
    if (command == "shadow-off" || command == "shadow-on") {
      const bool value = command == "shadow-on";
      if (use_c)
        native_window_set_has_shadow(alias, value);
      else
        target->SetHasShadow(value);
      return target->HasShadow() == value && native_window_has_shadow(alias) == value;
    }
    const auto preference = command == "square"  ? WindowCornerPreference::DoNotRound
                            : command == "small" ? WindowCornerPreference::RoundSmall
                            : command == "round" ? WindowCornerPreference::Round
                                                 : WindowCornerPreference::Default;
    return use_c ? native_window_set_corner_preference(
                       alias, static_cast<native_window_corner_preference_t>(preference))
                 : target->SetCornerPreference(preference);
  }
  void Verify() {
    Check(target &&
              target->GetCornerPreference() ==
                  static_cast<WindowCornerPreference>(native_window_get_corner_preference(alias)),
          "C++ and C alias share successful corner state");
    Check(target && target->HasShadow() == native_window_has_shadow(alias),
          "C++ and C alias share successful shadow state");
  }
};
inline StyleJournal style;
inline napi_value StyleState(napi_env env, napi_callback_info info) {
  size_t count = 1;
  napi_value arg = nullptr;
  napi_get_cb_info(env, info, &count, &arg, nullptr, nullptr);
  const auto command = count ? ohos::js::String(env, arg) : std::string();
  if (command == "setup")
    style.Setup();
  if (command == "verify")
    style.Verify();
  if (command == "verify-main")
    style.Check(style.main && style.main->GetNativeObject() &&
                    style.main->HasShadow() == native_window_has_shadow(style.main_alias),
                "main C++ and C alias agree after real SDK shadow completion");
  if (command == "finish") {
    style.Check(style.target && !style.target->GetNativeObject() &&
                    style.target->GetCornerPreference() == WindowCornerPreference::Default &&
                    !style.target->HasShadow() &&
                    !native_window_set_corner_preference(style.alias,
                                                         NATIVE_WINDOW_CORNER_PREFERENCE_ROUND),
                "retained closed wrappers reject mutations and return defaults");
    native_window_free(style.alias);
    native_window_free(style.main_alias);
    style.alias = style.main_alias = 0;
    OH_LOG_Print(LOG_APP, style.failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-style",
                 "NATIVEAPI_STYLE_FINAL_%{public}s passed=%{public}d failed=%{public}d",
                 style.failed ? "FAIL" : "PASS", style.passed, style.failed);
  }
  return ohos::String(env, style.State());
}
inline napi_value StyleAction(napi_env env, napi_callback_info info) {
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
    style.Check(style.Call(job->command, job->c),
                "UI style request submits or synchronous shadow operation succeeds");
    napi_resolve_deferred(env, job->deferred, ohos::String(env, style.State()));
    return promise;
  }
  napi_create_string_utf8(env, "nativeapi.window.style", NAPI_AUTO_LENGTH, &name);
  if (napi_create_async_work(
          env, nullptr, name,
          [](napi_env, void* p) {
            auto* j = static_cast<Work*>(p);
            j->result = style.Call(j->command, j->c);
          },
          [](napi_env env, napi_status status, void* p) {
            std::unique_ptr<Work> j(static_cast<Work*>(p));
            style.Check(
                status == napi_ok && j->result,
                "worker style request dispatches successfully; SDK state is checked afterwards");
            napi_resolve_deferred(env, j->deferred, ohos::String(env, style.State()));
            napi_delete_async_work(env, j->work);
          },
          job.get(), &job->work) != napi_ok ||
      napi_queue_async_work(env, job->work) != napi_ok) {
    if (job->work)
      napi_delete_async_work(env, job->work);
    style.Check(false, "worker style submission failed");
    napi_resolve_deferred(env, job->deferred, ohos::String(env, style.State()));
    return promise;
  }
  job.release();
  return promise;
}
}  // namespace nativeapi::tests
