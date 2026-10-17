#pragma once
#include <stdexcept>
#include "../../../../../../src/capi/event_request_c.h"

namespace nativeapi::tests {
struct NativeCloseJournal {
  struct Target {
    std::shared_ptr<Window> window, duplicate;
    native_window_t alias = 0;
    size_t object = 0, other = 0;
    native_listener_id_t c_listener = 0;
    std::string mode = "cancel";
    std::shared_ptr<EventRequest> request;
    std::shared_ptr<EventDecision> decision;
    native_event_decision_t c_decision = 0;
    int requests = 0, others = 0, c_requests = 0, closed = 0;
  };
  std::vector<std::unique_ptr<Target>> targets;
  Target* current = nullptr;
  Target* main = nullptr;
  int passed = 0, failed = 0, manager_requests = 0;
  size_t manager = 0;
  std::atomic<int> workers{0};
  void Check(bool ok, const char* label) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-native-close",
                 "%{public}s %{public}s", ok ? "PASS" : "FAIL", label);
  }
  void Listen(Target& t) {
    t.object = t.window->AddListener<WindowEvent>([this, &t](const auto& e) {
      Check(IsMainThread() && e.GetWindowId() == t.window->GetId(),
            "native object event keeps its identity and owning UI thread");
      if (dynamic_cast<const WindowClosedEvent*>(&e))
        ++t.closed;
      if (auto close = dynamic_cast<const WindowCloseRequestedEvent*>(&e)) {
        ++t.requests;
        t.request = close->GetRequest();
        Check(t.request && t.request->IsCancelable(), "native close carries a cancellable request");
        if (t.mode == "cancel")
          t.request->Cancel();
        if (t.mode == "defer")
          t.decision = t.request->Defer();
        OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-native-close",
                     "NATIVEAPI_NATIVE_CLOSE_EVENT %{public}d %{public}s", t.requests,
                     t.mode.c_str());
      }
    });
    t.other = t.duplicate->AddListener<WindowCloseRequestedEvent>([this, &t](const auto& e) {
      ++t.others;
      Check(e.GetRequest() == t.request, "duplicate wrapper observes the same native close vote");
      if (t.mode == "throw")
        throw std::runtime_error("deliberate fixture veto");
    });
    t.c_listener = native_window_add_listener(
        t.alias,
        [](const auto* e, void* p) {
          auto& t = *static_cast<Target*>(p);
          if (e->type == NATIVE_WINDOW_EVENT_TYPE_CLOSE_REQUESTED) {
            ++t.c_requests;
            if (t.mode == "c-cancel")
              native_event_request_cancel(e->data.close_requested.request);
            if (t.mode == "defer")
              t.c_decision = native_event_request_defer(e->data.close_requested.request);
          }
        },
        &t, nullptr);
    Check(t.object && t.other && t.c_listener, "C++ duplicate and generated C observers installed");
  }
  void Unlisten(Target& t) {
    if (t.object) {
      Check(t.window->RemoveListener(t.object) && t.duplicate->RemoveListener(t.other) &&
                native_window_remove_listener(t.alias, t.c_listener),
            "all native-close observers removed");
      t.object = t.other = t.c_listener = 0;
    }
  }
  void Attach(std::shared_ptr<Window> w, bool primary) {
    for (auto& old : targets)
      if (old.get() != main && !old->window->GetNativeObject() && old->alias) {
        Unlisten(*old);
        native_event_decision_free(old->c_decision);
        old->c_decision = 0;
        native_window_free(old->alias);
        old->alias = 0;
        old->decision.reset();
      }
    auto t = std::make_unique<Target>();
    t->window = std::move(w);
    Check(t->window && t->window->GetNativeObject(), "SDK-created real native window attached");
    if (!t->window)
      return;
    t->duplicate = std::make_shared<Window>(t->window->GetNativeObject());
    t->alias = native_window_manager_get(t->window->GetId());
    Check(t->alias && t->duplicate->GetId() == t->window->GetId(),
          "all aliases preserve the native SDK identity");
    current = t.get();
    if (primary)
      main = current;
    targets.push_back(std::move(t));
    Listen(*current);
  }
  std::string State() {
    auto* t = current;
    std::ostringstream out;
    out << "{\"passed\":" << passed << ",\"failed\":" << failed << ",\"workers\":" << workers.load()
        << ",\"managerRequests\":" << manager_requests
        << ",\"alive\":" << (t && t->window->GetNativeObject() ? "true" : "false")
        << ",\"requests\":" << (t ? t->requests : 0) << ",\"others\":" << (t ? t->others : 0)
        << ",\"cRequests\":" << (t ? t->c_requests : 0) << ",\"closed\":" << (t ? t->closed : 0)
        << ",\"pending\":" << (t && t->request && t->request->IsPending() ? "true" : "false")
        << ",\"id\":" << (t ? t->window->GetId() : 0) << ",\"systemId\":"
        << (t && t->window->GetNativeObject()
                ? ohos::FindWindowState(t->window->GetNativeObject())->system_id
                : 0)
        << "}";
    return out.str();
  }
  void Command(const std::string& command) {
    if (command == "setup") {
      manager = WindowManager::GetInstance().AddListener<WindowEvent>([this](const auto& e) {
        if (dynamic_cast<const WindowCloseRequestedEvent*>(&e))
          ++manager_requests;
      });
      Attach(Application::GetInstance().GetPrimaryWindow(), true);
    }
    if (command == "main")
      current = main;
    if (!current)
      return;
    auto& t = *current;
    if (command == "cancel" || command == "c-cancel" || command == "defer" || command == "accept" ||
        command == "throw")
      t.mode = command;
    if (command == "unlisten")
      Unlisten(t);
    if (command == "listen")
      Listen(t);
    if (command == "detach-all") {
      Unlisten(t);
      Unlisten(*main);
      Check(WindowManager::GetInstance().RemoveListener(manager),
            "last manager observer removed during a vote");
      manager = 0;
    }
    if (command == "attach-main")
      Listen(*main);
    if (command == "programmatic" || command == "coalesce") {
      const auto before = t.requests;
      const bool pending = t.request && t.request->IsPending();
      Check(t.window->Close(), "C++ child close request submits");
      if (command == "coalesce")
        Check(native_window_close(t.alias), "generated C close joins the pending native request");
      Check(t.requests == before + int(!pending && t.object != 0),
            "repeated SDK and API requests share one vote");
    }
    if (command == "main-programmatic")
      Check(!t.window->Close() && !native_window_close(t.alias),
            "Ability-owned main windows still reject programmatic destruction");
    if (command == "approve-a" || command == "approve-c" || command == "reject-c" ||
        command == "late") {
      auto decision = t.decision;
      auto c = t.c_decision;
      const bool cpp = command == "approve-a", accept = command != "reject-c",
                 late = command == "late";
      std::thread([this, decision, c, cpp, accept, late] {
        const bool ok = late  ? decision && !decision->Accept() && !native_event_decision_accept(c)
                        : cpp ? decision && decision->Accept()
                        : accept ? native_event_decision_accept(c)
                                 : native_event_decision_cancel(c);
        OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-native-close",
                     "%{public}s native close vote completed on a worker", ok ? "PASS" : "FAIL");
        ++workers;
      }).detach();
    }
    if (command == "verify") {
      Check(t.requests == t.others && t.requests == t.c_requests,
            "C++ duplicate and generated C close request counts agree");
      Check(manager_requests == 0, "manager observes windows without receiving close requests");
      if (!t.window->GetNativeObject())
        Check(!WindowManager::GetInstance().Get(t.window->GetId()) && !t.window->Close() &&
                  !native_window_close(t.alias) && t.closed == int(t.object != 0),
              "one actual native destruction invalidates retained aliases and manager lookup");
    }
  }
};
inline NativeCloseJournal native_close;
inline napi_value NativeCloseAttach(napi_env env, napi_callback_info info) {
  size_t count = 1;
  napi_value value = nullptr;
  napi_get_cb_info(env, info, &count, &value, nullptr, nullptr);
  auto main = Application::GetInstance().GetPrimaryWindow();
  native_close.Attach(ohos::AttachWindow(env, value, main->GetId()), false);
  return ohos::String(env, native_close.State());
}
inline napi_value NativeCloseState(napi_env env, napi_callback_info info) {
  size_t count = 2;
  napi_value args[2]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  const auto command = count ? ohos::js::String(env, args[0]) : std::string();
  native_close.Command(command);
  const auto request = count > 1 ? ohos::js::String(env, args[1]) : "";
  const auto state = native_close.State();
  OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-native-close",
               "NATIVEAPI_NATIVE_CLOSE_REPLY %{public}s %{public}s", request.c_str(),
               state.c_str());
  return ohos::String(env, state);
}
}  // namespace nativeapi::tests
