#pragma once

namespace nativeapi::tests {
struct LayoutJournal {
  FullscreenJournal base;
  struct Trace {
    LayoutJournal* owner = nullptr;
    int index = 0, behavior = 0, code = 0;
    napi_ref object = nullptr, method = nullptr;
    napi_deferred deferred = nullptr;
    bool ready = false, success = false;
    std::shared_ptr<ohos::WindowState> state;
  };
  std::array<Trace, 3> traces;
  std::vector<std::pair<int, bool>> sdk_calls;
  int calls = 0;
  bool last_result = false, observed = false;
  static napi_value Delivered(napi_env env, napi_callback_info info) {
    size_t count = 1; napi_value arg = nullptr; void* data = nullptr;
    napi_get_cb_info(env, info, &count, &arg, nullptr, &data);
    auto& t = *static_cast<Trace*>(data); t.ready = true; t.success = true; t.code = 0;
    return nullptr;
  }
  static napi_value Rejected(napi_env env, napi_callback_info info) {
    size_t count = 1; napi_value arg = nullptr; void* data = nullptr;
    napi_get_cb_info(env, info, &count, &arg, nullptr, &data);
    auto& t = *static_cast<Trace*>(data); t.ready = true; t.success = false;
    t.code = ModeJournal::Code(env, arg); return nullptr;
  }
  static napi_value Capture(napi_env env, napi_callback_info info) {
    size_t count = 1; napi_value arg = nullptr; void* data = nullptr;
    napi_get_cb_info(env, info, &count, &arg, nullptr, &data);
    auto& t = *static_cast<Trace*>(data); bool enabled = false;
    napi_get_value_bool(env, arg, &enabled);
    t.owner->base.base.Check(IsMainThread(), "layout SDK operation runs on UI");
    t.owner->sdk_calls.emplace_back(t.index, enabled);
    if (t.behavior == 2) { napi_throw_error(env, nullptr, "controlled layout SDK invocation fault"); return nullptr; }
    if (t.behavior == 3) { napi_value value = nullptr; napi_create_object(env, &value); return value; }
    napi_value object = nullptr, method = nullptr, result = nullptr;
    napi_get_reference_value(env, t.object, &object); napi_get_reference_value(env, t.method, &method);
    const auto status = napi_call_function(env, object, method, count, &arg, &result);
    if (t.behavior != 1) return status == napi_ok ? result : nullptr;
    t.behavior = 0; t.ready = false; t.success = false; t.code = 0;
    if (status != napi_ok) {
      napi_value error = nullptr; napi_get_and_clear_last_exception(env, &error);
      t.ready = true; t.code = ModeJournal::Code(env, error);
    }
    napi_value promise = nullptr;
    if (napi_create_promise(env, &t.deferred, &promise) != napi_ok) return nullptr;
    if (status == napi_ok) {
      napi_value good = nullptr, bad = nullptr, ignored = nullptr;
      const bool ok = napi_create_function(env, "layout.actual.success", NAPI_AUTO_LENGTH, Delivered, &t, &good) == napi_ok &&
          napi_create_function(env, "layout.actual.failure", NAPI_AUTO_LENGTH, Rejected, &t, &bad) == napi_ok &&
          ohos::js::Call(env, result, "then", {good, bad}, ignored);
      t.owner->base.base.Check(ok, "held layout result delegates to actual SDK promise");
    }
    return promise;
  }
  void Watch(napi_env env) {
    base.Watch();
    for (size_t i = 0; i < traces.size(); ++i) {
      auto& t = traces[i]; t.owner = this; t.index = i;
      t.state = ohos::FindWindowState(base.base.targets[i].window->GetNativeObject());
      napi_value object = ohos::WindowObject(t.state), method = nullptr, capture = nullptr;
      const bool ok = ohos::js::Property(env, object, "setWindowLayoutFullScreen", method) &&
          napi_create_reference(env, object, 1, &t.object) == napi_ok &&
          napi_create_reference(env, method, 1, &t.method) == napi_ok &&
          napi_create_function(env, "layout.actual.sdk", NAPI_AUTO_LENGTH, Capture, &t, &capture) == napi_ok &&
          napi_set_named_property(env, object, "setWindowLayoutFullScreen", capture) == napi_ok;
      base.base.Check(ok, "layout trace retains and delegates original SDK method");
    }
  }
  void Verify() {
    base.Verify();
    for (const auto& target : base.base.targets)
      base.base.Check(target.window->IsContentUnderTitleBar() == native_window_is_content_under_title_bar(target.alias),
                      "layout C++ and C live getter agreement");
  }
  std::string State() {
    std::ostringstream out;
    out << "{\"native\":" << base.State() << ",\"calls\":" << calls << ",\"result\":" << int(last_result)
        << ",\"observed\":" << int(observed) << ",\"layout\":[";
    for (size_t i = 0; i < traces.size(); ++i) {
      if (i) out << ','; out << int(base.base.targets[i].window->IsContentUnderTitleBar());
    }
    out << "],\"pending\":[";
    for (size_t i = 0; i < traces.size(); ++i) {
      if (i) out << ','; out << int(traces[i].state && traces[i].state->layout_request != nullptr);
    }
    out << "],\"held\":[";
    for (size_t i = 0; i < traces.size(); ++i) {
      if (i) out << ','; const auto& t = traces[i];
      out << '[' << int(t.deferred != nullptr) << ',' << int(t.ready) << ',' << int(t.success) << ',' << t.code << ']';
    }
    out << "],\"content\":[";
    for (size_t i = 0; i < traces.size(); ++i) {
      if (i) out << ','; const auto r = base.base.targets[i].window->GetContentBounds();
      out << "{\"x\":" << r.x << ",\"y\":" << r.y << ",\"width\":" << r.width << ",\"height\":" << r.height << '}';
    }
    out << "],\"sdkCalls\":[";
    for (size_t i = 0; i < sdk_calls.size(); ++i) {
      if (i) out << ','; out << '[' << sdk_calls[i].first << ',' << int(sdk_calls[i].second) << ']';
    }
    out << "]}"; return out.str();
  }
  void Finish(napi_env env) {
    Verify();
    for (auto& t : traces) {
      napi_value object = nullptr, method = nullptr;
      const bool ok = napi_get_reference_value(env, t.object, &object) == napi_ok &&
          napi_get_reference_value(env, t.method, &method) == napi_ok &&
          napi_set_named_property(env, object, "setWindowLayoutFullScreen", method) == napi_ok;
      base.base.Check(ok, "layout original SDK method restored");
      napi_delete_reference(env, t.object); napi_delete_reference(env, t.method); t.object = t.method = nullptr;
    }
    base.Finish();
    OH_LOG_Print(LOG_APP, base.base.failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-layout",
                 "NATIVEAPI_LAYOUT_FINAL_%{public}s", base.base.failed ? "FAIL" : "PASS");
  }
};
inline LayoutJournal layout;
inline napi_value LayoutState(napi_env env, napi_callback_info info) {
  size_t count = 2; napi_value args[2]{}; napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  const auto command = count ? ohos::js::String(env, args[0]) : std::string{};
  if (command == "setup") { layout.base.base.Setup(); return ohos::String(env, "{}"); }
  if (command == "attach-child" || command == "attach-ordinary") {
    layout.base.base.Bind(command == "attach-child" ? 1 : 2,
        ohos::AttachWindow(env, args[1], layout.base.base.targets[0].window->GetId()));
    return ohos::String(env, "{}");
  }
  if (command == "watch") layout.Watch(env);
  if (command == "verify") layout.Verify();
  if (command == "hold") layout.traces[1].behavior = 1;
  if (command == "fault") layout.traces[1].behavior = 2;
  if (command == "nonpromise") layout.traces[1].behavior = 3;
  if (command == "normal") layout.traces[1].behavior = 0;
  if (command == "release") {
    auto& t = layout.traces[1]; napi_value value = nullptr;
    layout.base.base.Check(t.ready && t.deferred, "actual held SDK outcome available before release");
    if (t.ready && t.deferred) {
      if (t.success) { napi_get_undefined(env, &value); napi_resolve_deferred(env, t.deferred, value); }
      else { napi_create_object(env, &value); napi_set_named_property(env, value, "code", ohos::Number(env, t.code)); napi_reject_deferred(env, t.deferred, value); }
      t.deferred = nullptr;
    }
  }
  if (command == "invalid") {
    Window empty, foreign(reinterpret_cast<void*>(uintptr_t{123}));
    auto stale = native_window_manager_get(layout.base.base.targets[0].window->GetId()); native_window_free(stale);
    layout.base.base.Check(!empty.SetContentUnderTitleBar(true) && !foreign.SetContentUnderTitleBar(true) &&
        !native_window_set_content_under_title_bar(0, true) && !native_window_set_content_under_title_bar(stale, true),
        "empty foreign zero and released layout mutations reject safely");
  }
  if (command == "finish") layout.Finish(env);
  return ohos::String(env, layout.State());
}
inline napi_value LayoutAction(napi_env env, napi_callback_info info) {
  size_t count = 4; napi_value args[4]{}; napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  int32_t index = -1;
  if (count != 4 || napi_get_value_int32(env, args[0], &index) != napi_ok || index < 0 || index > 2) return nullptr;
  struct Work {
    std::shared_ptr<Window> window; native_window_t alias = 0;
    bool enabled = false, use_c = false, worker = false, result = false, observed = false, correct = false;
    napi_deferred deferred{}; napi_async_work work{};
    void Execute() {
      correct = IsMainThread() != worker;
      result = use_c ? native_window_set_content_under_title_bar(alias, enabled) : window->SetContentUnderTitleBar(enabled);
      observed = use_c ? native_window_is_content_under_title_bar(alias) : window->IsContentUnderTitleBar();
    }
    void Complete(napi_env env) {
      ++layout.calls; layout.last_result = result; layout.observed = observed;
      layout.base.base.Check(correct, "layout public mutation runs on requested UI/worker thread");
      napi_value value = nullptr; napi_get_boolean(env, result, &value); napi_resolve_deferred(env, deferred, value);
    }
  };
  auto job = std::make_unique<Work>(); job->window = layout.base.base.targets[index].window; job->alias = layout.base.base.targets[index].alias;
  napi_get_value_bool(env, args[1], &job->enabled); napi_get_value_bool(env, args[2], &job->use_c); napi_get_value_bool(env, args[3], &job->worker);
  napi_value promise = nullptr, name = nullptr; if (napi_create_promise(env, &job->deferred, &promise) != napi_ok) return nullptr;
  if (!job->worker) { job->Execute(); job->Complete(env); return promise; }
  napi_create_string_utf8(env, "nativeapi.layout", NAPI_AUTO_LENGTH, &name);
  if (napi_create_async_work(env, nullptr, name,
      [](napi_env, void* data) { static_cast<Work*>(data)->Execute(); },
      [](napi_env env, napi_status status, void* data) {
        std::unique_ptr<Work> job(static_cast<Work*>(data)); job->correct &= status == napi_ok; job->Complete(env); napi_delete_async_work(env, job->work);
      }, job.get(), &job->work) != napi_ok || napi_queue_async_work(env, job->work) != napi_ok) {
    if (job->work) napi_delete_async_work(env, job->work);
    layout.base.base.Check(false, "layout async worker submission failed"); napi_value no = nullptr; napi_get_boolean(env, false, &no); napi_resolve_deferred(env, job->deferred, no); return promise;
  }
  job.release(); return promise;
}
}  // namespace nativeapi::tests
