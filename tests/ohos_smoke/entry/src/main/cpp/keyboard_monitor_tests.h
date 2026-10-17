#pragma once
#include "../../../../../../src/keyboard_monitor.h"
#include "../../../../../../src/capi/keyboard_monitor_c.h"
#include <multimodalinput/oh_input_manager.h>
#include <accesstoken/ability_access_control.h>
#include <array>
#include <thread>

namespace nativeapi::tests {
inline void KeyboardSdkProbe(const Input_KeyEvent*) {}
struct KeyboardJournal {
  std::unique_ptr<KeyboardMonitor> a, b;
  native_keyboard_monitor_t c = 0;
  size_t a_listener = 0;
  native_listener_id_t c_listener = 0;
  std::array<std::vector<std::pair<int, int>>, 3> events;
  int passed = 0, failed = 0, permission = -1, released = 0;
  bool stop_a = false, delete_a = false, free_c = false, granted = false;
  void Check(bool ok, const char* name) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_WARN : LOG_ERROR, 1, "nativeapi-monitor",
                 "%{public}s %{public}s", ok ? "PASS" : "FAIL", name);
  }
  void Event(int index, int action, int key) {
    Check(IsMainThread(), "actual native keyboard event delivered on UI");
    events[index].emplace_back(action, key);
    if (action != 1 || key != 2098) return;
    if (index == 0 && stop_a) { stop_a = false; a->Stop(); }
    if (index == 0 && delete_a) { delete_a = false; a.reset(); }
    if (index == 2 && free_c) { free_c = false; native_keyboard_monitor_free(c); c = 0; }
  }
  size_t Listen(KeyboardMonitor& monitor, int index) {
    return monitor.AddListener<KeyboardEvent>([this, index](const KeyboardEvent& event) {
      const auto pressed = dynamic_cast<const KeyPressedEvent*>(&event);
      const auto released = dynamic_cast<const KeyReleasedEvent*>(&event);
      Check(pressed || released, "NDK event retains its concrete key event type");
      if (pressed || released) Event(index, pressed ? 1 : 2, event.GetKeycode());
    });
  }
};
inline KeyboardJournal keyboard_journal;
inline bool keyboard_uninitialized = false;
inline void InitializeKeyboardTest() {
  KeyboardMonitor early;
  early.Start();
  keyboard_uninitialized = !early.IsMonitoring();
  early.Stop();
}
inline napi_value KeyboardState(napi_env env, napi_callback_info info) {
  napi_value args[2]{}, result = nullptr;
  size_t count = 2;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  const auto command = count ? ohos::js::String(env, args[0]) : std::string();
  const auto request = count > 1 ? ohos::js::String(env, args[1]) : std::string();
  auto& j = keyboard_journal;
  if (command == "setup") {
    j.Check(keyboard_uninitialized, "Start before runtime initialization remains inactive");
    j.granted = OH_AT_CheckSelfPermission("ohos.permission.INPUT_MONITORING");
    j.permission = OH_Input_AddKeyEventMonitor(KeyboardSdkProbe);
    j.Check(j.permission == INPUT_SUCCESS || j.permission == INPUT_PERMISSION_DENIED,
            "direct SDK reports either authorized monitor or actual permission refusal");
    if (j.permission == INPUT_SUCCESS)
      j.Check(OH_Input_RemoveKeyEventMonitor(KeyboardSdkProbe) == INPUT_SUCCESS,
              "independent direct SDK probe unregisters successfully");
    j.a = std::make_unique<KeyboardMonitor>();
    j.b = std::make_unique<KeyboardMonitor>();
    j.c = native_keyboard_monitor_create();
    j.a_listener = j.Listen(*j.a, 0);
    j.Listen(*j.b, 1);
    j.c_listener = native_keyboard_monitor_add_listener(j.c,
      [](const native_keyboard_event_t* event, void* raw) {
        auto& j = *static_cast<KeyboardJournal*>(raw);
        j.Check(event && (event->type == NATIVE_KEYBOARD_EVENT_TYPE_KEY_PRESSED ||
                          event->type == NATIVE_KEYBOARD_EVENT_TYPE_KEY_RELEASED),
                "C ABI preserves actual native key event type and payload");
        if (event) j.Event(2, event->type == NATIVE_KEYBOARD_EVENT_TYPE_KEY_PRESSED ? 1 : 2, event->keycode);
      }, &j, [](void* raw) { ++static_cast<KeyboardJournal*>(raw)->released; });
    j.Check(j.c && j.a_listener && j.c_listener, "independent C++ and C monitor listeners created");
    j.Check(!j.a->IsMonitoring() && !j.b->IsMonitoring() && !native_keyboard_monitor_is_monitoring(j.c),
            "adding listeners does not implicitly start explicit monitoring");
    j.a->Start(); j.a->Start(); j.b->Start(); native_keyboard_monitor_start(j.c);
    const bool granted = j.permission == INPUT_SUCCESS;
    j.Check(j.a->IsMonitoring() == granted && j.b->IsMonitoring() == granted &&
            native_keyboard_monitor_is_monitoring(j.c) == granted,
            "all logical monitor states agree with real SDK permission and installation");
  } else if (command == "clear") {
    for (auto& events : j.events) events.clear();
  } else if (command == "stop-a") {
    j.a->Stop(); j.a->Stop();
    j.Check(!j.a->IsMonitoring(), "repeated Stop is inactive and idempotent");
  } else if (command == "start-a") {
    j.a->Start();
    j.Check(j.a->IsMonitoring() == (j.permission == INPUT_SUCCESS), "restart obtains a fresh observation epoch");
  } else if (command == "stop-all" || command == "worker-stop") {
    const auto stop = [&] { if (j.a) j.a->Stop(); if (j.b) j.b->Stop(); native_keyboard_monitor_stop(j.c); };
    if (command == "worker-stop") { std::thread worker(stop); worker.join(); }
    else stop();
    j.Check((!j.a || !j.a->IsMonitoring()) && (!j.b || !j.b->IsMonitoring()) &&
            !native_keyboard_monitor_is_monitoring(j.c), "all monitors stop including last native subscription");
  } else if (command == "worker-start") {
    std::thread worker([&] { j.a->Start(); if (j.b) j.b->Start(); native_keyboard_monitor_start(j.c); });
    worker.join();
    j.Check(j.a->IsMonitoring() == (j.permission == INPUT_SUCCESS), "worker registration completes without waiting for UI");
  } else if (command == "hold") {
    display_test_dispatcher.Hold();
  } else if (command == "replace-drain") {
    size_t held;
    { std::lock_guard<std::mutex> lock(display_test_dispatcher.mutex); held = display_test_dispatcher.queued.size(); }
    j.Check(held >= 6, "real native SDK events accumulated while UI delivery was held");
    j.a->Stop(); j.a->Start();
    j.b.reset();
    display_test_dispatcher.Drain();
    j.Check(j.events[0].empty() && j.events[1].empty(), "old queued epochs and destroyed C++ emitter cannot deliver");
  } else if (command == "self-stop") {
    j.stop_a = true;
  } else if (command == "self-delete") {
    j.delete_a = true;
  } else if (command == "self-free") {
    j.free_c = true;
  } else if (command == "remove-a") {
    j.a->RemoveListener(j.a_listener);
  } else if (command == "listen-a") {
    j.a_listener = j.Listen(*j.a, 0);
  } else if (command == "finish") {
    if (j.a) j.a->Stop();
    j.a.reset(); j.b.reset();
    native_keyboard_monitor_free(j.c);
    j.Check(!native_keyboard_monitor_is_monitoring(j.c), "released C monitor handles fail safely");
    native_keyboard_monitor_free(j.c); j.c = 0;
    display_test_dispatcher.Drain();
  } else if (command == "finish-check") {
    j.Check(j.released == 1, "C listener user data released exactly once after asynchronous cleanup");
  }
  std::ostringstream out;
  out << "{\"passed\":" << j.passed << ",\"failed\":" << j.failed
      << ",\"permission\":" << j.permission << ",\"granted\":" << (j.granted ? "true" : "false") << ",\"released\":" << j.released
      << ",\"aActive\":" << (j.a && j.a->IsMonitoring() ? "true" : "false")
      << ",\"bActive\":" << (j.b && j.b->IsMonitoring() ? "true" : "false")
      << ",\"cActive\":" << (native_keyboard_monitor_is_monitoring(j.c) ? "true" : "false") << ",\"events\":[";
  for (size_t i = 0; i < j.events.size(); ++i) {
    if (i) out << ',';
    out << '[';
    for (size_t k = 0; k < j.events[i].size(); ++k) {
      if (k) out << ',';
      out << '[' << j.events[i][k].first << ',' << j.events[i][k].second << ']';
    }
    out << ']';
  }
  out << "]}";
  OH_LOG_Print(LOG_APP, LOG_WARN, 1, "nativeapi-monitor", "NATIVEAPI_MONITOR_REPLY %{public}s %{public}s %{public}s",
               request.c_str(), command.c_str(), out.str().c_str());
  napi_create_string_utf8(env, out.str().c_str(), NAPI_AUTO_LENGTH, &result);
  return result;
}
}  // namespace nativeapi::tests
