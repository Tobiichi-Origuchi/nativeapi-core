#pragma once
#include "../../../../../../src/capi/window_c.h"
#include "../../../../../../src/capi/window_manager_c.h"
#include "../../../../../../src/platform/ohos/ohos_napi.h"

namespace nativeapi::tests {
inline const bool occlusion_before_host =
    Window::IsOcclusionStateSupported() || native_window_is_occlusion_state_supported();
struct OcclusionJournal {
  std::shared_ptr<Window> target, cover;
  native_window_t alias = 0;
  size_t object_listener = 0, manager_listener = 0;
  native_listener_id_t c_listener = 0;
  int passed = 0, failed = 0, objects = 0, managers = 0, c_events = 0;
  void Check(bool ok, const char* message) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-occlusion",
                 "%{public}s %{public}s", ok ? "PASS" : "FAIL", message);
  }
  std::string State() {
    std::ostringstream s;
    s << "{\"ready\":" << (target && cover ? "true" : "false")
      << ",\"state\":" << (target ? int(target->GetOcclusionState()) : -1)
      << ",\"objects\":" << objects << ",\"managers\":" << managers << ",\"cEvents\":" << c_events
      << ",\"passed\":" << passed << ",\"failed\":" << failed << "}";
    return s.str();
  }
  void Listen() {
    if (object_listener || !target)
      return;
    object_listener = target->AddListener<WindowOcclusionChangedEvent>([this](const auto& e) {
      ++objects;
      Check(IsMainThread() && e.GetWindowId() == target->GetId() &&
                e.GetOcclusionState() == target->GetOcclusionState(),
            "object occlusion event exposes current SDK-backed state on UI");
    });
    manager_listener = WindowManager::GetInstance().AddListener<WindowOcclusionChangedEvent>(
        [this](const auto& e) {
          if (e.GetWindowId() == target->GetId())
            ++managers;
        });
    c_listener = native_window_add_listener(
        alias,
        [](const native_window_event_t* e, void* p) {
          auto& j = *static_cast<OcclusionJournal*>(p);
          if (e->type == NATIVE_WINDOW_EVENT_TYPE_OCCLUSION_CHANGED) {
            ++j.c_events;
            j.Check(IsMainThread() && e->data.occlusion_changed.occlusion_state ==
                                          native_window_get_occlusion_state(j.alias),
                    "C ABI event exposes the same current SDK occlusion");
          }
        },
        this, nullptr);
    Check(object_listener && manager_listener && c_listener,
          "all three public observation paths registered");
  }
  void Unlisten() {
    if (!object_listener)
      return;
    Check(target->RemoveListener(object_listener) &&
              WindowManager::GetInstance().RemoveListener(manager_listener) &&
              native_window_remove_listener(alias, c_listener),
          "all public observers removed");
    object_listener = manager_listener = c_listener = 0;
  }
  void Setup() {
    Check(!occlusion_before_host, "capability query before host initialization is false");
    Check(Window::IsOcclusionStateSupported() && native_window_is_occlusion_state_supported(),
          "initialized SessionManager capability available through C++ and C");
    Window invalid(reinterpret_cast<void*>(uintptr_t{123}));
    Window empty;
    Check(invalid.GetOcclusionState() == WindowOcclusionState::Unknown &&
              empty.GetOcclusionState() == WindowOcclusionState::Unknown &&
              native_window_get_occlusion_state(0) == NATIVE_WINDOW_OCCLUSION_STATE_UNKNOWN,
          "invalid native tokens and empty C++/C handles remain Unknown");
    Check(ohos::CreateSubWindow("NativeAPI Occlusion Target", "pages/OcclusionTarget",
                                [this](auto w) {
                                  target = w;
                                  Check(bool(target), "real occlusion target created");
                                  if (!target)
                                    return;
                                  alias = native_window_manager_get(target->GetId());
                                  target->SetBounds({80, 120, 160, 140});
                                  target->SetBackgroundColor(Color::FromRGBA(255, 0, 255));
                                  Listen();
                                  Check(ohos::CreateSubWindow(
                                            "NativeAPI Occlusion Cover", "pages/OcclusionCover",
                                            [this](auto w) {
                                              cover = w;
                                              Check(bool(cover), "real occluding child created");
                                              if (cover)
                                                cover->SetBackgroundColor(
                                                    Color::FromRGBA(0, 255, 255));
                                            }),
                                        "occluding child creation submitted");
                                }),
          "target creation submitted");
  }
  void Action(const std::string& action) {
    if (!target || !cover)
      return;
    if (action == "unlisten") {
      Unlisten();
      return;
    }
    if (action == "relisten") {
      Listen();
      return;
    }
    if (action == "close") {
      target->Close();
      cover->Close();
      return;
    }
    if (action == "hidden") {
      cover->Hide();
      target->Hide();
      return;
    }
    target->Show();
    if (action == "uncovered" || action == "reshown" || action == "unobserved-visible") {
      cover->Hide();
    } else {
      cover->SetBounds(action == "partial" ? Rectangle{160, 120, 160, 140}
                                           : Rectangle{50, 90, 220, 200});
      cover->SetBackgroundColor(action == "transparent" ? Color::FromRGBA(0, 255, 255, 128)
                                                        : Color::FromRGBA(0, 255, 255));
      cover->Show();
    }
  }
  bool Query(int expected, bool shown) {
    return target && int(target->GetOcclusionState()) == expected &&
           int(native_window_get_occlusion_state(alias)) == expected &&
           target->IsVisible() == shown && native_window_is_visible(alias) == shown &&
           (expected != 0 ||
            (!target->GetNativeObject() && !native_window_get_native_object(alias))) &&
           Window::IsOcclusionStateSupported() == native_window_is_occlusion_state_supported();
  }
  void Verify(bool result) {
    Check(result, "C++/C query agrees with independent SDK and expected native scene");
    Check(Application::GetInstance().IsVisible(),
          "child occlusion/hiding does not change owning application visibility");
    Check(objects == managers && objects == c_events,
          "object, manager and C ABI events agree without duplicate aggregate transitions");
  }
};
inline OcclusionJournal occlusion;
inline napi_value OcclusionCommand(napi_env env, napi_callback_info info) {
  napi_value arg = nullptr;
  size_t count = 1;
  napi_get_cb_info(env, info, &count, &arg, nullptr, nullptr);
  const auto command = ohos::js::String(env, arg);
  if (command == "setup")
    occlusion.Setup();
  else if (command == "finish") {
    occlusion.Unlisten();
    native_window_free(occlusion.alias);
    occlusion.alias = 0;
    OH_LOG_Print(LOG_APP, occlusion.failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-occlusion",
                 "NATIVEAPI_OCCLUSION_FINAL_%{public}s passed=%{public}d failed=%{public}d",
                 occlusion.failed ? "FAIL" : "PASS", occlusion.passed, occlusion.failed);
  } else if (command != "state")
    occlusion.Action(command);
  return ohos::String(env, occlusion.State());
}
inline napi_value OcclusionCheck(napi_env env, napi_callback_info info) {
  size_t count = 3;
  napi_value args[3]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  struct Work {
    napi_deferred deferred{};
    napi_async_work work{};
    int expected = -1;
    bool shown = false;
    bool result = false;
  };
  auto job = std::make_unique<Work>();
  bool worker = false;
  napi_get_value_int32(env, args[0], &job->expected);
  napi_get_value_bool(env, args[1], &worker);
  napi_get_value_bool(env, args[2], &job->shown);
  napi_value promise = nullptr, name = nullptr;
  if (napi_create_promise(env, &job->deferred, &promise) != napi_ok)
    return nullptr;
  if (!worker) {
    occlusion.Verify(occlusion.Query(job->expected, job->shown));
    napi_resolve_deferred(env, job->deferred, ohos::String(env, occlusion.State()));
    return promise;
  }
  napi_create_string_utf8(env, "nativeapi.occlusion", NAPI_AUTO_LENGTH, &name);
  if (napi_create_async_work(
          env, nullptr, name,
          [](napi_env, void* p) {
            auto* j = static_cast<Work*>(p);
            j->result = occlusion.Query(j->expected, j->shown);
          },
          [](napi_env env, napi_status status, void* p) {
            std::unique_ptr<Work> j(static_cast<Work*>(p));
            occlusion.Verify(status == napi_ok && j->result);
            napi_resolve_deferred(env, j->deferred, ohos::String(env, occlusion.State()));
            napi_delete_async_work(env, j->work);
          },
          job.get(), &job->work) != napi_ok ||
      napi_queue_async_work(env, job->work) != napi_ok) {
    if (job->work)
      napi_delete_async_work(env, job->work);
    occlusion.Check(false, "worker submission failed");
    napi_resolve_deferred(env, job->deferred, ohos::String(env, occlusion.State()));
    return promise;
  }
  job.release();
  return promise;
}
}  // namespace nativeapi::tests
