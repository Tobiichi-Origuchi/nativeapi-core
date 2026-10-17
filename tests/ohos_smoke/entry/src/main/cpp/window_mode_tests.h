#pragma once
#include "../../../../../../src/platform/ohos/ohos_window_modes.h"

namespace nativeapi::tests {
struct ModeJournal {
  TitleJournal base;
  struct Target {
    ModeJournal* journal = nullptr;
    WindowId id = 0;
    size_t object_listener = 0;
    native_listener_id_t c_listener = 0;
    std::array<int, 3> events{};
    std::shared_ptr<ohos::WindowState> state;
  };
  std::array<Target, 3> targets;
  struct Trace {
    ModeJournal* journal = nullptr;
    int index = 0;
    napi_ref object = nullptr, method = nullptr;
    napi_deferred deferred = nullptr;
    bool hold = false, ready = false, success = false;
    int code = 0;
  };
  std::array<Trace, 4> traces;
  std::vector<std::pair<int, uint32_t>> sdk_calls;
  size_t manager_listener = 0;
  napi_ref original_method = nullptr;
  bool valid = true;
  int calls = 0, window_calls = 0;
  bool worker_observed = false;
  static int Code(napi_env env, napi_value error) {
    napi_value value = nullptr; int32_t code = -1;
    if (ohos::js::Property(env, error, "code", value)) napi_get_value_int32(env, value, &code);
    return code;
  }
  static napi_value Fulfilled(napi_env env, napi_callback_info info) {
    size_t count = 0; void* data = nullptr;
    napi_get_cb_info(env, info, &count, nullptr, nullptr, &data);
    auto& trace = *static_cast<Trace*>(data);
    trace.ready = trace.success = true; trace.code = 0; return nullptr;
  }
  static napi_value Rejected(napi_env env, napi_callback_info info) {
    size_t count = 1; void* data = nullptr; napi_value error = nullptr;
    napi_get_cb_info(env, info, &count, &error, nullptr, &data);
    auto& trace = *static_cast<Trace*>(data);
    trace.ready = true; trace.success = false; trace.code = Code(env, error); return nullptr;
  }
  static napi_value Capture(napi_env env, napi_callback_info info) {
    napi_value args[2]{}; size_t count = 2; void* data = nullptr;
    napi_get_cb_info(env, info, &count, args, nullptr, &data);
    auto& trace = *static_cast<Trace*>(data);
    trace.journal->valid &= IsMainThread();
    uint32_t length = 0, mask = 0;
    if (count && napi_get_array_length(env, args[0], &length) == napi_ok && length <= 3) {
      for (uint32_t i = 0; i < length; ++i) {
        napi_value value = nullptr; int32_t mode = -1;
        if (napi_get_element(env, args[0], i, &value) == napi_ok &&
            napi_get_value_int32(env, value, &mode) == napi_ok && mode >= 0 && mode <= 2)
          mask |= 1u << mode;
      }
    }
    trace.journal->sdk_calls.emplace_back(trace.index, mask);
    napi_value object = nullptr, method = nullptr, result = nullptr;
    if (napi_get_reference_value(env, trace.object, &object) != napi_ok ||
        napi_get_reference_value(env, trace.method, &method) != napi_ok) return nullptr;
    const auto status = napi_call_function(env, object, method, count, args, &result);
    if (!trace.hold) return status == napi_ok ? result : nullptr;
    trace.hold = false;
    // Delay only delivery of the real SDK outcome, never fabricate success.
    if (status != napi_ok) {
      napi_value error = nullptr;
      napi_get_and_clear_last_exception(env, &error);
      trace.ready = true; trace.success = false; trace.code = Code(env, error);
    }
    napi_value promise = nullptr;
    if (napi_create_promise(env, &trace.deferred, &promise) != napi_ok) return nullptr;
    if (status == napi_ok) {
      napi_value fulfilled = nullptr, rejected = nullptr, ignored = nullptr;
      if (napi_create_function(env, "mode.fulfilled", NAPI_AUTO_LENGTH, Fulfilled, &trace, &fulfilled) != napi_ok ||
          napi_create_function(env, "mode.rejected", NAPI_AUTO_LENGTH, Rejected, &trace, &rejected) != napi_ok ||
          !ohos::js::Call(env, result, "then", {fulfilled, rejected}, ignored)) {
        trace.ready = true; trace.success = false; trace.code = -2;
      }
    }
    return promise;
  }
  void TraceMethods(napi_env env) {
    for (size_t i = 0; i < traces.size(); ++i) {
      auto state = ohos::FindWindowState(base.targets[i < 3 ? i : 0].window->GetNativeObject());
      napi_value object = i < 3 ? ohos::WindowObject(state) : ohos::WindowStageObject(state);
      napi_value method = nullptr, wrapper = nullptr; auto& trace = traces[i];
      trace.journal = this; trace.index = int(i);
      const bool ok = ohos::js::Property(env, object, "setSupportedWindowModes", method) &&
          napi_create_reference(env, object, 1, &trace.object) == napi_ok &&
          napi_create_reference(env, method, 1, &trace.method) == napi_ok &&
          napi_create_function(env, "mode.sdk.trace", NAPI_AUTO_LENGTH, Capture, &trace, &wrapper) == napi_ok &&
          napi_set_named_property(env, object, "setSupportedWindowModes", wrapper) == napi_ok;
      base.Check(ok, "mode SDK call tracing delegates to the real window/Stage API");
    }
  }
  void Watch(napi_env env) {
    TraceMethods(env);
    for (size_t i = 0; i < targets.size(); ++i) {
      auto& t = targets[i]; t.journal = this; t.id = base.targets[i].window->GetId();
      t.state = ohos::FindWindowState(base.targets[i].window->GetNativeObject());
      t.object_listener = base.targets[i].window->AddListener<WindowPropertyChangedEvent>(
          [this, i](const auto& event) {
            if (event.GetProperty() != WindowProperty::FullScreenable) return;
            valid &= IsMainThread() && event.GetWindowId() == targets[i].id;
            ++targets[i].events[0];
          });
      t.c_listener = native_window_add_listener(base.targets[i].alias,
          [](const native_window_event_t* event, void* data) {
            auto& t = *static_cast<Target*>(data);
            if (event->type != NATIVE_WINDOW_EVENT_TYPE_PROPERTY_CHANGED ||
                event->data.property_changed.property != NATIVE_WINDOW_PROPERTY_FULL_SCREENABLE) return;
            t.journal->valid &= IsMainThread() && event->window_id == t.id;
            ++t.events[2];
          }, &t, nullptr);
      base.Check(t.object_listener && t.c_listener, "fullscreen policy object/C listeners installed");
    }
    manager_listener = WindowManager::GetInstance().AddListener<WindowPropertyChangedEvent>(
        [this](const auto& event) {
          if (event.GetProperty() != WindowProperty::FullScreenable) return;
          valid &= IsMainThread();
          for (auto& t : targets) if (event.GetWindowId() == t.id) ++t.events[1];
        });
    base.Check(manager_listener != 0, "fullscreen policy manager listener installed");
  }
  void Verify() {
    base.Verify();
    for (size_t i = 0; i < targets.size(); ++i) {
      const auto& t = targets[i]; const auto& b = base.targets[i];
      base.Check(valid && t.events[0] == t.events[1] && t.events[1] == t.events[2],
                 "fullscreen policy object/manager/C events agree on identity and UI");
      base.Check(b.window->IsFullScreenable() == native_window_is_full_screenable(b.alias),
                 "fullscreen policy C++/C queries agree");
    }
  }
  bool Method(napi_env env, bool missing) {
    const auto state = ohos::FindWindowState(base.targets[0].window->GetNativeObject());
    if (!state) return false;
    auto object = ohos::WindowObject(state); napi_value method = nullptr;
    if (missing) {
      if (original_method || !ohos::js::Property(env, object, "setSupportedWindowModes", method) ||
          napi_create_reference(env, method, 1, &original_method) != napi_ok) return false;
      napi_get_undefined(env, &method);
      return napi_set_named_property(env, object, "setSupportedWindowModes", method) == napi_ok;
    }
    if (!original_method || napi_get_reference_value(env, original_method, &method) != napi_ok ||
        napi_set_named_property(env, object, "setSupportedWindowModes", method) != napi_ok) return false;
    napi_delete_reference(env, original_method); original_method = nullptr; return true;
  }
  std::string State() {
    std::ostringstream out;
    out << "{\"native\":" << base.State() << ",\"calls\":" << calls
        << ",\"windowCalls\":" << window_calls << ",\"observed\":" << (worker_observed ? "true" : "false") << ",\"policy\":[";
    for (size_t i = 0; i < targets.size(); ++i) {
      if (i) out << ','; out << (base.targets[i].window->IsFullScreenable() ? "true" : "false");
    }
    out << "],\"fullscreen\":[";
    for (size_t i = 0; i < targets.size(); ++i) {
      if (i) out << ','; out << (base.targets[i].window->IsFullScreen() ? "true" : "false");
    }
    out << "],\"events\":[";
    for (size_t i = 0; i < targets.size(); ++i) {
      if (i) out << ','; const auto& e = targets[i].events;
      out << '[' << e[0] << ',' << e[1] << ',' << e[2] << ']';
    }
    out << "],\"masks\":[";
    for (size_t i = 0; i < targets.size(); ++i) {
      if (i) out << ',';
      const auto state = targets[i].state;
      out << (state && state->supported_window_modes ? int(*state->supported_window_modes) : -1);
    }
    out << "],\"pending\":[";
    for (size_t i = 0; i < targets.size(); ++i) {
      if (i) out << ',';
      const auto state = targets[i].state;
      out << (state ? state->mode_requests.size() : 0);
    }
    out << "],\"sdkCalls\":[";
    for (size_t i = 0; i < sdk_calls.size(); ++i) {
      if (i) out << ','; out << '[' << sdk_calls[i].first << ',' << sdk_calls[i].second << ']';
    }
    out << "],\"held\":[";
    for (size_t i = 0; i < traces.size(); ++i) {
      if (i) out << ','; const auto& t = traces[i];
      out << '[' << int(t.deferred != nullptr) << ',' << int(t.ready) << ',' << int(t.success) << ',' << t.code << ']';
    }
    out << "]}"; return out.str();
  }
  void Finish(napi_env env) {
    Verify();
    if (original_method) base.Check(Method(env, false), "SDK method restored before finishing");
    for (auto& trace : traces) {
      napi_value object = nullptr, method = nullptr;
      const bool ok = trace.object && trace.method &&
          napi_get_reference_value(env, trace.object, &object) == napi_ok &&
          napi_get_reference_value(env, trace.method, &method) == napi_ok &&
          napi_set_named_property(env, object, "setSupportedWindowModes", method) == napi_ok;
      base.Check(ok, "original SDK mode method restored");
      if (trace.method) napi_delete_reference(env, trace.method);
      if (trace.object) napi_delete_reference(env, trace.object);
      trace.method = trace.object = nullptr;
    }
    for (size_t i = 0; i < targets.size(); ++i) {
      base.Check(base.targets[i].window->RemoveListener(targets[i].object_listener) &&
                   native_window_remove_listener(base.targets[i].alias, targets[i].c_listener),
                 "fullscreen policy target listeners removed");
    }
    base.Check(WindowManager::GetInstance().RemoveListener(manager_listener), "mode manager listener removed");
    base.Finish();
    OH_LOG_Print(LOG_APP, base.failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-mode",
                 "NATIVEAPI_MODE_FINAL_%{public}s", base.failed ? "FAIL" : "PASS");
  }
};
inline ModeJournal modes;
inline napi_value ModeState(napi_env env, napi_callback_info info) {
  napi_value args[2]{}; size_t count = 2;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  const auto command = count ? ohos::js::String(env, args[0]) : std::string{};
  if (command == "setup") { modes.base.Setup(); return ohos::String(env, "{}"); }
  if (command == "attach-child" || command == "attach-ordinary") {
    modes.base.Bind(command == "attach-child" ? 1 : 2,
        ohos::AttachWindow(env, args[1], modes.base.targets[0].window->GetId()));
    return ohos::String(env, "{}");
  }
  if (command == "watch") modes.Watch(env);
  if (command == "verify") modes.Verify();
  if (command == "hold-child") {
    modes.base.Check(!modes.traces[1].deferred, "child SDK completion can be held once");
    modes.traces[1].hold = true;
  }
  if (command == "release-held") {
    auto& trace = modes.traces[1]; napi_value value = nullptr;
    napi_status result = napi_invalid_arg;
    if (trace.ready && trace.deferred) {
      if (trace.success) {
        napi_get_undefined(env, &value); result = napi_resolve_deferred(env, trace.deferred, value);
      } else {
        napi_create_object(env, &value); napi_set_named_property(env, value, "code", ohos::Number(env, trace.code));
        result = napi_reject_deferred(env, trace.deferred, value);
      }
      trace.deferred = nullptr;
    }
    modes.base.Check(result == napi_ok, "actual late SDK outcome released after native destruction");
  }
  if (command == "fallback-on" || command == "fallback-off")
    modes.base.Check(modes.Method(env, command == "fallback-on"), "primary SDK method availability fault applied/restored");
  if (command == "invalid") {
    Window empty, foreign(reinterpret_cast<void*>(uintptr_t{123}));
    empty.SetFullScreenable(true); foreign.SetFullScreenable(false); native_window_set_full_screenable(0, false);
    modes.base.Check(!empty.IsFullScreenable() && !foreign.IsFullScreenable() && !native_window_is_full_screenable(0),
                     "invalid fullscreen policy wrappers are inert");
    const auto stale = native_window_manager_get(modes.base.targets[0].window->GetId());
    native_window_free(stale); native_window_set_full_screenable(stale, false);
    modes.base.Check(!native_window_is_full_screenable(stale), "released policy handle remains inert");
  }
  if (command == "finish") modes.Finish(env);
  return ohos::String(env, modes.State());
}
inline napi_value ModeSeed(napi_env env, napi_callback_info info) {
  napi_value args[2]{}; size_t count = 2; int32_t index = -1;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  if (count != 2 || napi_get_value_int32(env, args[0], &index) != napi_ok || index < 0 || index >= 3) return nullptr;
  uint32_t length = 0; std::vector<int32_t> values;
  if (napi_get_array_length(env, args[1], &length) != napi_ok || length > 4) return nullptr;
  for (uint32_t i = 0; i < length; ++i) {
    napi_value value = nullptr; int32_t mode = -1;
    if (napi_get_element(env, args[1], i, &value) != napi_ok || napi_get_value_int32(env, value, &mode) != napi_ok) return nullptr;
    values.push_back(mode);
  }
  return ohos::Boolean(env, ohos::SetWindowModeBaseline(modes.base.targets[index].window, values));
}
inline napi_value ModeAction(napi_env env, napi_callback_info info) {
  napi_value args[6]{}; size_t count = 6;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  struct Work {
    std::shared_ptr<Window> window; native_window_t alias = 0; std::string operation;
    bool enabled = false, use_c = false, worker = false, batch = false, valid_thread = false, observed = false;
    napi_deferred deferred{}; napi_async_work work{};
    void Execute() {
      valid_thread = IsMainThread() != worker;
      if (operation == "fullscreen") {
        if (use_c) native_window_set_full_screen(alias, enabled); else window->SetFullScreen(enabled);
        observed = window->IsFullScreenable(); return;
      }
      for (int i = 0; i < (batch ? 3 : 1); ++i) {
        const bool value = i == 1 ? !enabled : enabled;
        if (use_c != (i == 1)) native_window_set_full_screenable(alias, value);
        else window->SetFullScreenable(value);
      }
      observed = use_c ? native_window_is_full_screenable(alias) : window->IsFullScreenable();
    }
    void Complete(napi_env env) {
      if (operation == "fullscreen") ++modes.window_calls; else modes.calls += batch ? 3 : 1;
      modes.worker_observed = observed;
      modes.base.Check(valid_thread, "fullscreen policy uses requested execution thread");
      napi_resolve_deferred(env, deferred, ohos::String(env, "submitted"));
    }
  };
  int32_t index = -1;
  if (count != 6 || napi_get_value_int32(env, args[0], &index) != napi_ok || index < 0 || index >= 3) return nullptr;
  auto job = std::make_unique<Work>(); job->window = modes.base.targets[index].window; job->alias = modes.base.targets[index].alias;
  napi_get_value_bool(env, args[1], &job->enabled); napi_get_value_bool(env, args[2], &job->use_c);
  napi_get_value_bool(env, args[3], &job->worker); napi_get_value_bool(env, args[4], &job->batch);
  job->operation = ohos::js::String(env, args[5]);
  napi_value promise = nullptr, name = nullptr;
  if (napi_create_promise(env, &job->deferred, &promise) != napi_ok) return nullptr;
  if (!job->worker) { job->Execute(); job->Complete(env); return promise; }
  napi_create_string_utf8(env, "nativeapi.mode", NAPI_AUTO_LENGTH, &name);
  if (napi_create_async_work(env, nullptr, name,
      [](napi_env, void* data) { static_cast<Work*>(data)->Execute(); },
      [](napi_env env, napi_status status, void* data) {
        std::unique_ptr<Work> job(static_cast<Work*>(data));
        job->valid_thread &= status == napi_ok; job->Complete(env); napi_delete_async_work(env, job->work);
      }, job.get(), &job->work) != napi_ok || napi_queue_async_work(env, job->work) != napi_ok) {
    if (job->work) napi_delete_async_work(env, job->work);
    modes.base.Check(false, "mode worker submission failed");
    napi_resolve_deferred(env, job->deferred, ohos::String(env, "failed")); return promise;
  }
  job.release(); return promise;
}
}  // namespace nativeapi::tests
