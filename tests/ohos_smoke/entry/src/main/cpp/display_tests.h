#pragma once
#include <array>
#include "../../../../../../src/capi/common_c.h"
#include "../../../../../../src/capi/display_c.h"
#include "../../../../../../src/capi/display_manager_c.h"
#include "../../../../../../src/platform/ohos/display_ohos.h"

namespace nativeapi::tests {
struct DisplayJournal {
  std::shared_ptr<Display> held;
  native_display_t alias = 0;
  size_t cpp_listener = 0, typed_listener = 0;
  native_listener_id_t c_listener = 0, async_listener = 0;
  std::atomic<int> sdk_changes{0}, sdk_areas{0}, sdk_adds{0}, sdk_removes{0};
  std::array<uint32_t, 4> reference_ids{};
  std::array<bool, 4> reference_ok{};
  int cpp = 0, typed = 0, c = 0, async_c = 0, added = 0, removed = 0;
  int passed = 0, failed = 0;
  bool references = false;
  void Check(bool ok, const char* message) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-displays",
                 "%{public}s %{public}s", ok ? "PASS" : "FAIL", message);
  }
  void Listen() {
    auto& manager = DisplayManager::GetInstance();
    if (!cpp_listener)
      cpp_listener = manager.AddListener<DisplayEvent>([this](const auto& e) {
        if (dynamic_cast<const DisplayChangedEvent*>(&e))
          ++cpp;
        if (dynamic_cast<const DisplayAddedEvent*>(&e))
          ++added;
        if (dynamic_cast<const DisplayRemovedEvent*>(&e))
          ++removed;
        Check(IsMainThread() && e.GetDisplay() &&
                  (e.GetTypeName() == "DisplayRemovedEvent" || e.GetDisplay()->GetNativeObject()),
              "C++ display events arrive on UI with a live identity or removed reference");
        if (e.GetTypeName() == "DisplayChangedEvent")
          Check(e.GetDisplay() == held && e.GetDisplay()->GetId() == held->GetId(),
                "change event preserves the held display identity");
        OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-displays",
                     "NATIVEAPI_DISPLAY_EVENT %{public}s", e.GetTypeName().c_str());
      });
    if (!typed_listener)
      typed_listener = manager.AddListener<DisplayChangedEvent>([this](const auto&) { ++typed; });
    if (!c_listener)
      c_listener = native_display_manager_add_listener(
          [](const native_display_event_t* e, void* data) {
            auto& j = *static_cast<DisplayJournal*>(data);
            if (e->type == NATIVE_DISPLAY_EVENT_TYPE_CHANGED)
              ++j.c;
            j.Check(IsMainThread() && native_display_get_id(e->display) == j.held->GetId(),
                    "borrowed C event payload shares the live display identity on UI");
          },
          this, nullptr);
    if (!async_listener)
      async_listener = native_display_manager_add_listener_async(
          [](const native_display_event_t* e, native_event_delivery_t delivery, void* data) {
            auto& j = *static_cast<DisplayJournal*>(data);
            if (e->type == NATIVE_DISPLAY_EVENT_TYPE_CHANGED)
              ++j.async_c;
            j.Check(IsMainThread() && native_event_delivery_is_active(delivery) &&
                        native_display_get_id(e->display) == j.held->GetId(),
                    "asynchronous C event lease carries the same display on UI");
            j.Check(native_event_delivery_complete(delivery, false),
                    "display event delivery lease completes");
          },
          this, nullptr);
    Check(cpp_listener && typed_listener && c_listener && async_listener,
          "all four real display event subscription paths installed");
  }
  void Unlisten() {
    auto& manager = DisplayManager::GetInstance();
    if (cpp_listener)
      Check(manager.RemoveListener(cpp_listener), "C++ base observer removed");
    if (typed_listener)
      Check(manager.RemoveListener(typed_listener), "C++ typed observer removed");
    if (c_listener)
      Check(native_display_manager_remove_listener(c_listener), "C observer removed");
    if (async_listener)
      Check(native_display_manager_remove_listener(async_listener), "async C observer removed");
    cpp_listener = typed_listener = c_listener = async_listener = 0;
  }
  void Verify() {
    auto current = DisplayManager::GetInstance().GetPrimary();
    NativeDisplayManager_DisplayInfo* raw = nullptr;
    const auto code = OH_NativeDisplayManager_CreatePrimaryDisplay(&raw);
    Check(code == DISPLAY_MANAGER_OK && raw && held && current == held,
          "SDK and manager retain the same connected primary display");
    if (!raw || !held)
      return;
    const auto scale = raw->densityPixels > 0 ? raw->densityPixels : 1;
    const auto size = held->GetSize();
    const auto c_size = native_display_get_size(alias);
    Check(std::abs(size.width * scale - raw->width) < .01 &&
              std::abs(size.height * scale - raw->height) < .01 && size.width == c_size.width &&
              size.height == c_size.height,
          "held C++ and C display sizes track actual SDK pixels and density");
    Check(held->GetScaleFactor() == native_display_get_scale_factor(alias) &&
              std::abs(held->GetScaleFactor() - scale) < .0001 && held->IsPrimary() &&
              native_display_is_primary(alias),
          "live scale and primary queries agree across SDK C++ and C");
    const int orientations[] = {0, 90, 180, 270};
    Check(int(raw->orientation) >= 0 && int(raw->orientation) < 4 &&
              int(held->GetOrientation()) == orientations[int(raw->orientation)] &&
              to_cpp_display_orientation(native_display_get_orientation(alias)) ==
                  held->GetOrientation(),
          "held orientation updates through both C++ and generated C ABI");
    Check(held->GetRefreshRate() == int(raw->refreshRate) &&
              native_display_get_refresh_rate(alias) == int(raw->refreshRate),
          "refresh rate matches the real SDK");
    char* c_name = native_display_get_name(alias);
    Check(c_name && held->GetName() == raw->name && held->GetName() == c_name,
          "display names are read live and copied through C ABI");
    free_c_str(c_name);
    const auto position = held->GetPosition();
    const auto c_position = native_display_get_position(alias);
    Check(position.x == c_position.x && position.y == c_position.y,
          "C++ and C position getters use the same logical desktop coordinates");
    NativeDisplayManager_Rect* area = nullptr;
    OH_NativeDisplayManager_CreateAvailableArea(raw->id, &area);
    const auto work = held->GetWorkArea();
    const auto c_work = native_display_get_work_area(alias);
    const Rectangle expected =
        area ? Rectangle{position.x + area->left / scale, position.y + area->top / scale,
                         area->width / scale, area->height / scale}
             : Rectangle{position.x, position.y, raw->width / scale, raw->height / scale};
    Check(std::abs(work.x - expected.x) < .01 && std::abs(work.y - expected.y) < .01 &&
              std::abs(work.width - expected.width) < .01 &&
              std::abs(work.height - expected.height) < .01 && work.x == c_work.x &&
              work.y == c_work.y && work.width == c_work.width && work.height == c_work.height,
          "live work area agrees with independent SDK geometry and the documented fallback");
    if (area)
      OH_NativeDisplayManager_DestroyAvailableArea(area);
    Check(held->GetBitDepth() == 0 && native_display_get_bit_depth(alias) == 0,
          "unavailable framebuffer depth is reported as unknown through C++ and C");
    OH_NativeDisplayManager_DestroyDisplay(raw);
  }
  std::string State() {
    NativeDisplayManager_DisplayInfo* raw = nullptr;
    const auto code = OH_NativeDisplayManager_CreatePrimaryDisplay(&raw);
    std::ostringstream s;
    s << "{\"cpp\":" << cpp << ",\"typed\":" << typed << ",\"c\":" << c << ",\"async\":" << async_c
      << ",\"added\":" << added << ",\"removed\":" << removed
      << ",\"sdkChanged\":" << sdk_changes.load() << ",\"sdkArea\":" << sdk_areas.load()
      << ",\"sdkAdded\":" << sdk_adds.load() << ",\"sdkRemoved\":" << sdk_removes.load()
      << ",\"pending\":" << ohos::PendingDisplayNotifications()
      << ",\"listeners\":" << DisplayManager::GetInstance().GetTotalListenerCount()
      << ",\"id\":" << (held ? held->GetId() : 0) << ",\"passed\":" << passed
      << ",\"failed\":" << failed << ",\"sdkCode\":" << code
      << ",\"rotation\":" << (raw ? int(raw->rotation) : -1)
      << ",\"orientation\":" << (raw ? int(raw->orientation) : -1)
      << ",\"width\":" << (raw ? raw->width : 0) << ",\"height\":" << (raw ? raw->height : 0)
      << ",\"scale\":" << (raw ? raw->densityPixels : 0) << '}';
    NativeDisplayManager_Rect* area = nullptr;
    const auto area_code = raw ? OH_NativeDisplayManager_CreateAvailableArea(raw->id, &area)
                               : DISPLAY_MANAGER_ERROR_INVALID_PARAM;
    const auto win = Application::GetInstance().GetPrimaryWindow();
    auto result = s.str();
    result.pop_back();
    std::ostringstream extra;
    extra << result << ",\"full\":" << (win && win->IsFullScreen() ? "true" : "false")
          << ",\"areaCode\":" << int(area_code) << ",\"area\":[" << (area ? area->left : 0) << ','
          << (area ? area->top : 0) << ',' << (area ? area->width : 0) << ','
          << (area ? area->height : 0) << "]}";
    if (area)
      OH_NativeDisplayManager_DestroyAvailableArea(area);
    if (raw)
      OH_NativeDisplayManager_DestroyDisplay(raw);
    return extra.str();
  }
};
inline DisplayJournal displays;
inline void DisplayReferenceChanged(uint64_t) {
  ++displays.sdk_changes;
}
inline void DisplayReferenceArea(uint64_t) {
  ++displays.sdk_areas;
}
inline void DisplayReferenceAdded(uint64_t) {
  ++displays.sdk_adds;
}
inline void DisplayReferenceRemoved(uint64_t) {
  ++displays.sdk_removes;
}
inline napi_value DisplayState(napi_env env, napi_callback_info info) {
  napi_value args[2];
  size_t count = 2;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  const auto command = count ? ohos::js::String(env, args[0]) : std::string();
  const auto request = count > 1 ? ohos::js::String(env, args[1]) : std::string();
  auto& j = displays;
  if (command == "setup" && !j.held) {
    j.held = DisplayManager::GetInstance().GetPrimary();
    j.alias = native_display_manager_get_primary();
    j.Check(j.held && j.alias && native_display_get_id(j.alias) == j.held->GetId(),
            "C++ and C aliases refer to the same real primary display");
    auto& ids = j.reference_ids;
    j.reference_ok[0] = OH_NativeDisplayManager_RegisterDisplayChangeListener(
                            DisplayReferenceChanged, &ids[0]) == DISPLAY_MANAGER_OK;
    j.reference_ok[1] = OH_NativeDisplayManager_RegisterAvailableAreaChangeListener(
                            DisplayReferenceArea, &ids[1]) == DISPLAY_MANAGER_OK;
    j.reference_ok[2] = OH_NativeDisplayManager_RegisterDisplayAddListener(
                            DisplayReferenceAdded, &ids[2]) == DISPLAY_MANAGER_OK;
    j.reference_ok[3] = OH_NativeDisplayManager_RegisterDisplayRemoveListener(
                            DisplayReferenceRemoved, &ids[3]) == DISPLAY_MANAGER_OK;
    j.Check(j.reference_ok[0] && j.reference_ok[1] && j.reference_ok[2] && j.reference_ok[3],
            "independent direct SDK reference listeners installed");
    j.references = true;
    j.Listen();
    j.Verify();
  }
  if (command == "fullscreen" || command == "windowed") {
    auto win = Application::GetInstance().GetPrimaryWindow();
    if (win)
      win->SetFullScreen(command == "fullscreen");
  }
  if (command == "verify")
    j.Verify();
  if (command == "unlisten")
    j.Unlisten();
  if (command == "listen")
    j.Listen();
  if (command == "listen-worker") {
    std::thread work([&] { j.Listen(); });
    work.join();
  }
  if (command == "unlisten-worker") {
    std::thread work([&] { j.Unlisten(); });
    work.join();
  }
  if (command == "remove-c") {
    j.Check(native_display_manager_remove_listener(j.c_listener),
            "partial C unsubscription succeeds");
    j.c_listener = 0;
  }
  if (command == "hold-queue")
    display_test_dispatcher.Hold();
  if (command == "release-queue")
    display_test_dispatcher.Drain();
  if (command == "replace-queue") {
    j.Check(ohos::PendingDisplayNotifications() > 0,
            "real native SDK notifications are pending before observer replacement");
    const auto before = std::array<int, 4>{j.cpp, j.typed, j.c, j.async_c};
    j.Unlisten();
    j.Listen();
    display_test_dispatcher.Drain();
    j.Check(before == std::array<int, 4>{j.cpp, j.typed, j.c, j.async_c},
            "draining real old notifications cannot reach replaced observers");
  }
  if (command == "finish") {
    j.Unlisten();
    display_test_dispatcher.Drain();
    if (j.references) {
      if (j.reference_ok[0])
        OH_NativeDisplayManager_UnregisterDisplayChangeListener(j.reference_ids[0]);
      if (j.reference_ok[1])
        OH_NativeDisplayManager_UnregisterAvailableAreaChangeListener(j.reference_ids[1]);
      if (j.reference_ok[2])
        OH_NativeDisplayManager_UnregisterDisplayAddListener(j.reference_ids[2]);
      if (j.reference_ok[3])
        OH_NativeDisplayManager_UnregisterDisplayRemoveListener(j.reference_ids[3]);
      j.references = false;
    }
    native_display_free(j.alias);
    j.alias = 0;
    OH_LOG_Print(LOG_APP, j.failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-displays",
                 "NATIVEAPI_DISPLAY_FINAL_%{public}s (%{public}d passed, %{public}d failed)",
                 j.failed ? "FAIL" : "PASS", j.passed, j.failed);
  }
  const auto state = j.State();
  OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-displays",
               "NATIVEAPI_DISPLAY_STATE %{public}s %{public}s", command.c_str(), state.c_str());
  if (!request.empty())
    OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-displays",
                 "NATIVEAPI_DISPLAY_REPLY %{public}s %{public}s %{public}s", request.c_str(),
                 command.c_str(), state.c_str());
  napi_value result;
  napi_create_string_utf8(env, state.c_str(), state.size(), &result);
  return result;
}
}  // namespace nativeapi::tests
