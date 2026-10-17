#pragma once
#include <atomic>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include "../../../../../../src/application.h"
#include "../../../../../../src/capi/application_c.h"
#include "../../../../../../src/platform/ohos/ohos_application.h"

namespace nativeapi::tests {
struct AppJournal {
  int started = 0, activated = 0, deactivated = 0, exited = 0, requests = 0;
  int c_started = 0, c_activated = 0, c_deactivated = 0, c_exited = 0, c_requests = 0;
  int quit_mode = 0;
  int passed = 0, failed = 0;
  bool uninitialized = false, attached_metadata = false;
  std::shared_ptr<EventRequest> request;
  std::shared_ptr<EventDecision> vote;
  native_event_decision_t c_vote = 0;
  std::mutex mutex;
  void Check(bool ok, const char* label) {
    std::lock_guard<std::mutex> lock(mutex);
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-app", "%{public}s %{public}s",
                 ok ? "PASS" : "FAIL", label);
  }
};
inline AppJournal app_journal;
inline void InstallApplicationJournal() {
  auto& app = Application::GetInstance();
  auto& j = app_journal;
  j.uninitialized = !app.IsRunning() && !app.IsVisible() && !app.IsSingleInstance() &&
      !app.GetPrimaryWindow() && app.GetAllWindows().empty() &&
      !app.SetBrightness(Brightness::Dark) && !native_application_is_running();
  app.AddListener<ApplicationEvent>([](const ApplicationEvent& event) {
    auto& j = app_journal;
    j.Check(IsMainThread(), "application event is delivered on ArkTS UI thread");
    if (dynamic_cast<const ApplicationStartedEvent*>(&event)) ++j.started;
    if (dynamic_cast<const ApplicationActivatedEvent*>(&event)) ++j.activated;
    if (dynamic_cast<const ApplicationDeactivatedEvent*>(&event)) ++j.deactivated;
    if (auto exit = dynamic_cast<const ApplicationExitingEvent*>(&event)) {
      ++j.exited;
      j.Check(!Application::GetInstance().IsRunning() && !Application::GetInstance().IsVisible() &&
                  !Application::GetInstance().GetPrimaryWindow(), "real destroy event exposes ended host state");
      j.Check(exit->GetExitCode() == (j.quit_mode == 3 ? 99 : 0), "actual Ability exit preserves approved request exit code");
      OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-app", "NATIVEAPI_APP_EXIT code=%{public}d exited=%{public}d", exit->GetExitCode(), j.exited);
    }
    if (auto quit = dynamic_cast<const ApplicationQuitRequestedEvent*>(&event)) {
      ++j.requests;
      j.request = quit->GetRequest();
      if (j.quit_mode == 1)
        j.Check(j.request->Cancel(), "C++ listener vetoes programmatic quit");
      else if (j.quit_mode == 2 || j.quit_mode == 3)
        j.vote = j.request->Defer();
      else if (!j.request->IsCancelable())
        j.Check(!j.request->Cancel() && !j.request->Defer(), "required native destruction cannot be vetoed or deferred");
    }
    OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-app", "NATIVEAPI_APP_EVENT %{public}s running=%{public}d visible=%{public}d",
                 event.GetTypeName().c_str(), Application::GetInstance().IsRunning(), Application::GetInstance().IsVisible());
  });
  native_application_add_listener([](const native_application_event_t* event, void*) {
    auto& j = app_journal;
    switch (event->type) {
      case NATIVE_APPLICATION_EVENT_TYPE_STARTED: ++j.c_started; break;
      case NATIVE_APPLICATION_EVENT_TYPE_ACTIVATED:
        ++j.c_activated;
        j.Check(native_application_is_running() && native_application_is_visible(), "C ABI foreground event exposes live visible Ability state");
        break;
      case NATIVE_APPLICATION_EVENT_TYPE_DEACTIVATED:
        ++j.c_deactivated;
        j.Check(native_application_is_running() && !native_application_is_visible(), "C ABI background event preserves running but clears visible state");
        break;
      case NATIVE_APPLICATION_EVENT_TYPE_EXITING:
        ++j.c_exited;
        j.Check(j.c_exited == 1 && j.exited == 1 && !native_application_is_running() &&
                    !native_application_is_visible(), "C ABI observes one real exit with ended lifecycle state");
        OH_LOG_Print(LOG_APP, j.failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-app", "NATIVEAPI_APP_FINAL_%{public}s passed=%{public}d failed=%{public}d", j.failed ? "FAIL" : "PASS", j.passed, j.failed);
        break;
      case NATIVE_APPLICATION_EVENT_TYPE_QUIT_REQUESTED:
        ++j.c_requests;
        if (j.quit_mode == 2 || j.quit_mode == 3)
          j.c_vote = native_event_request_defer(event->data.quit_requested.request);
        else if (j.quit_mode == 1)
          j.Check(native_event_request_is_cancelled(event->data.quit_requested.request), "C ABI listener observes shared C++ veto");
        break;
    }
    OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-app", "NATIVEAPI_APP_C_EVENT %{public}d", event->type);
  }, nullptr, nullptr);
}
inline napi_value ApplicationLifecycle(napi_env env, napi_callback_info info) {
  size_t count = 2;
  napi_value args[2]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  const auto name = count == 2 ? ohos::js::String(env, args[1]) : "";
  ohos::AbilityLifecycle kind;
  if (name == "create") kind = ohos::AbilityLifecycle::Create;
  else if (name == "foreground") kind = ohos::AbilityLifecycle::Foreground;
  else if (name == "background") kind = ohos::AbilityLifecycle::Background;
  else if (name == "destroy") kind = ohos::AbilityLifecycle::Destroy;
  else return ohos::Boolean(env, false);
  const bool ok = ohos::HandleApplicationLifecycle(env, args[0], kind);
  OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-app", "HOST_APPLICATION_LIFECYCLE %{public}s accepted=%{public}d", name.c_str(), ok);
  return ohos::Boolean(env, ok);
}
inline napi_value RunApplication(napi_env env, napi_callback_info) {
  auto& j = app_journal;
  auto& app = Application::GetInstance();
  j.Check(j.uninitialized, "application queries and appearance fail closed before host initialization");
  j.Check(j.attached_metadata, "application metadata is available after context attachment before lifecycle forwarding");
  j.Check(app.IsRunning() && native_application_is_running(), "actual created Ability is running through C++ and C ABI");
  j.Check(app.IsVisible() && native_application_is_visible(), "foreground Ability is visible through C++ and C ABI");
  j.Check(j.started == 1 && j.c_started == 1 && j.activated >= 1 && j.c_activated == j.activated,
          "real startup and foreground callbacks emit one C++ and C ABI event each");
  bool singleton = false;
  ohos::WithContext([&](napi_env env, napi_value context) {
    napi_value info = nullptr, mode = nullptr, type = nullptr;
    int32_t value = -1;
    if (ohos::js::Property(env, context, "applicationInfo", info) &&
        ohos::js::Property(env, info, "multiAppMode", mode) &&
        ohos::js::Property(env, mode, "multiAppModeType", type))
      singleton = napi_get_value_int32(env, type, &value) == napi_ok && value == 0;
  });
  j.Check(app.IsSingleInstance() == singleton && singleton && native_application_is_single_instance(),
          "single-instance state comes from real application multi-instance/clone configuration");
  auto main = app.GetPrimaryWindow();
  j.Check(main && main->GetNativeObject() && app.GetAllWindows().size() == 1,
          "application discovers live attached main window");
  auto c_main = native_application_get_primary_window();
  j.Check(c_main && main && native_window_get_id(c_main) == main->GetId(), "C ABI primary-window lookup preserves native identity");
  native_window_free(c_main);
  app.SetPrimaryWindow(nullptr);
  j.Check(!app.GetPrimaryWindow() && !native_application_get_primary_window(), "explicit null clears primary selection");
  app.SetPrimaryWindow(main);
  j.Check(app.GetPrimaryWindow() == main, "explicit primary window round-trips");
  j.Check(app.Run() == -1 && app.Run(nullptr) == -1 && native_application_run() == -1,
          "host-owned Ability loop is not falsely reported as a completed native loop");
  j.Check(!app.SetBrightness(static_cast<Brightness>(99)) && !native_application_set_brightness(static_cast<native_brightness_t>(99)),
          "invalid appearance enum is rejected without changing the SDK");
  if (std::string(OH_GetDeviceType()) != "2in1")
    j.Check(!app.Show() && !app.Hide(), "phone rejects desktop application-level hiding");
  j.Check(!app.SetIcon("") && !app.SetDockIconVisible(false) && !app.SetProgressBar(.5) &&
              !app.SetBadgeLabel("invalid") && !app.SetMenuBar(nullptr), "remaining unimplemented application integrations do not report success");
  OH_LOG_Print(LOG_APP, j.failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-app", "NATIVEAPI_APP_BASE_%{public}s passed=%{public}d failed=%{public}d", j.failed ? "FAIL" : "PASS", j.passed, j.failed);
  napi_value result;
  napi_create_string_utf8(env, j.failed ? "NATIVEAPI_APP_BASE_FAIL" : "NATIVEAPI_APP_BASE_PASS", NAPI_AUTO_LENGTH, &result);
  return result;
}
struct AppThemeWork {
  napi_deferred deferred;
  napi_async_work work;
  Brightness mode;
  bool use_c = false, result = false;
};
inline napi_value SetApplicationTheme(napi_env env, napi_callback_info info) {
  size_t count = 3;
  napi_value args[3]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  int32_t mode = -1;
  bool use_c = false, worker = false;
  if (count != 3 || napi_get_value_int32(env, args[0], &mode) != napi_ok ||
      napi_get_value_bool(env, args[1], &use_c) != napi_ok || napi_get_value_bool(env, args[2], &worker) != napi_ok)
    return nullptr;
  if (!worker) {
    bool ok = use_c ? native_application_set_brightness(static_cast<native_brightness_t>(mode)) :
                     Application::GetInstance().SetBrightness(static_cast<Brightness>(mode));
    app_journal.Check(ok, "UI-thread application appearance reaches real SDK");
    napi_value promise;
    napi_deferred deferred;
    napi_create_promise(env, &deferred, &promise);
    napi_resolve_deferred(env, deferred, ohos::Boolean(env, ok));
    return promise;
  }
  auto task = std::make_unique<AppThemeWork>();
  task->mode = static_cast<Brightness>(mode);
  task->use_c = use_c;
  napi_value promise, name;
  napi_create_promise(env, &task->deferred, &promise);
  napi_create_string_utf8(env, "nativeapi.appThemeTest", NAPI_AUTO_LENGTH, &name);
  napi_create_async_work(env, nullptr, name, [](napi_env, void* raw) {
    auto& t = *static_cast<AppThemeWork*>(raw);
    auto& app = Application::GetInstance();
    auto window = app.GetPrimaryWindow();
    auto c_window = native_application_get_primary_window();
    app_journal.Check(app.IsRunning() && app.IsVisible() && app.IsSingleInstance() &&
                          window && app.GetAllWindows().size() == 1 && c_window &&
                          native_window_get_id(c_window) == window->GetId(),
                      "worker application state and primary-window queries preserve real native identity");
    native_window_free(c_window);
    t.result = t.use_c ? native_application_set_brightness(static_cast<native_brightness_t>(t.mode)) :
                        Application::GetInstance().SetBrightness(t.mode);
  }, [](napi_env env, napi_status status, void* raw) {
    std::unique_ptr<AppThemeWork> t(static_cast<AppThemeWork*>(raw));
    app_journal.Check(status == napi_ok && t->result, "worker application appearance marshals to SDK UI environment");
    napi_resolve_deferred(env, t->deferred, ohos::Boolean(env, status == napi_ok && t->result));
    napi_delete_async_work(env, t->work);
  }, task.get(), &task->work);
  napi_queue_async_work(env, task->work);
  task.release();
  return promise;
}
struct AppQuitTest {
  uv_timer_t timer{};
  int mode = 0, before = 0, c_before = 0, ticks = 0;
  std::atomic<bool> submitted{false};
  void Tick() {
    if (!submitted && ++ticks < 200)
      return;
    auto& j = app_journal;
    auto& app = Application::GetInstance();
    j.Check(submitted && j.requests == before + 1 && j.c_requests == c_before + 1,
            "worker quit produces one shared C++ and C ABI confirmation");
    const bool pending = j.vote && j.vote->IsPending() && j.c_vote && native_event_decision_is_pending(j.c_vote);
    j.Check(pending && app.IsRunning() && j.exited == 0, "two deferred language votes keep the actual Ability alive");
    app.Quit(77);
    native_application_quit(99);
    j.Check(j.requests == before + 1 && j.c_requests == c_before + 1,
            "duplicate requests share confirmation and update the exit code");
    if (pending) {
      j.Check(j.vote->Accept() && app.IsRunning() && native_event_decision_is_pending(j.c_vote),
              "accepting one vote does not terminate before the other language accepts");
    }
    uv_timer_stop(&timer);
    const int selected = mode;
    uv_close(reinterpret_cast<uv_handle_t*>(&timer), [](uv_handle_t* h) { delete static_cast<std::shared_ptr<AppQuitTest>*>(h->data); });
    if (!pending)
      return;
    const auto c_vote = std::exchange(j.c_vote, 0);
    if (selected == 2) {
      j.Check(native_event_decision_cancel(c_vote) && !j.vote->Accept() &&
                  !native_event_decision_cancel(c_vote), "C ABI veto wins and rejects late duplicate decisions");
      native_event_decision_free(c_vote);
      j.Check(app.IsRunning() && app.IsVisible() && j.exited == 0, "deferred cancellation leaves the real host running");
      OH_LOG_Print(LOG_APP, j.failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-app", "NATIVEAPI_APP_DEFER_CANCEL_%{public}s", j.failed ? "FAIL" : "PASS");
    } else {
      OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-app", "NATIVEAPI_APP_ACCEPTING_QUIT code=99");
      std::thread([c_vote] {
        app_journal.Check(native_event_decision_accept(c_vote), "worker C ABI acceptance resumes approved SDK termination");
        native_event_decision_free(c_vote);
      }).detach();
    }
  }
};
inline napi_value RunApplicationQuit(napi_env env, napi_callback_info info) {
  size_t count = 1;
  napi_value value = nullptr;
  napi_get_cb_info(env, info, &count, &value, nullptr, nullptr);
  int32_t mode = 0;
  if (!count || napi_get_value_int32(env, value, &mode) != napi_ok || mode < 1 || mode > 4)
    return ohos::Boolean(env, false);
  auto& j = app_journal;
  j.quit_mode = mode;
  if (mode == 4)
    return ohos::Boolean(env, true);
  if (mode == 1) {
    int before = j.requests;
    Application::GetInstance().Quit(7);
    j.Check(j.requests == before + 1 && j.c_requests == j.requests && j.request && j.request->IsCancelled(),
            "cancelled quit is shared across native bindings");
    j.Check(Application::GetInstance().IsRunning() && j.exited == 0, "veto prevents actual SDK termination");
    OH_LOG_Print(LOG_APP, j.failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-app", "NATIVEAPI_APP_CANCEL_%{public}s", j.failed ? "FAIL" : "PASS");
    return ohos::Boolean(env, !j.failed);
  }
  auto test = std::make_shared<AppQuitTest>();
  test->mode = mode;
  test->before = j.requests;
  test->c_before = j.c_requests;
  uv_loop_t* loop = nullptr;
  napi_get_uv_event_loop(env, &loop);
  uv_timer_init(loop, &test->timer);
  test->timer.data = new std::shared_ptr<AppQuitTest>(test);
  uv_timer_start(&test->timer, [](uv_timer_t* timer) {
    (*static_cast<std::shared_ptr<AppQuitTest>*>(timer->data))->Tick();
  }, 100, 100);
  std::thread([test] {
    native_application_quit(42);
    test->submitted = true;
  }).detach();
  return ohos::Boolean(env, true);
}
}  // namespace nativeapi::tests
