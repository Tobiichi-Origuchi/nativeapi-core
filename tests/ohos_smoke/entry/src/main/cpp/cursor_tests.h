#pragma once
#include <multimodalinput/oh_input_manager.h>
#include "../../../../../../src/capi/display_manager_c.h"

namespace nativeapi::tests {
inline napi_value CursorProbe(napi_env env, napi_callback_info info) {
  napi_value args[3];
  size_t count = 3;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  const auto phase = count ? ohos::js::String(env, args[0]) : std::string();
  double event_x = 0, event_y = 0;
  if (count == 3) {
    napi_get_value_double(env, args[1], &event_x);
    napi_get_value_double(env, args[2], &event_y);
  }
  int32_t display_id = -1, after_id = -1;
  double x = 0, y = 0, after_x = 0, after_y = 0;
  const auto code = OH_Input_GetPointerLocation(&display_id, &x, &y);
  const auto cpp = DisplayManager::GetInstance().GetCursorPosition();
  const auto c = native_display_manager_get_cursor_position();
  Point worker_cpp;
  native_point_t worker_c{};
  Input_Result worker_code = INPUT_SUCCESS;
  int32_t worker_id = -1;
  double worker_x = 0, worker_y = 0;
  std::thread worker([&] {
    worker_code = OH_Input_GetPointerLocation(&worker_id, &worker_x, &worker_y);
    worker_cpp = DisplayManager::GetInstance().GetCursorPosition();
    worker_c = native_display_manager_get_cursor_position();
  });
  worker.join();
  const auto after_code = OH_Input_GetPointerLocation(&after_id, &after_x, &after_y);
  NativeDisplayManager_DisplayInfo* primary = nullptr;
  OH_NativeDisplayManager_CreatePrimaryDisplay(&primary);
  const double scale = primary && primary->densityPixels > 0 ? primary->densityPixels : 1;
  const bool primary_match = primary && code == INPUT_SUCCESS && display_id >= 0 &&
                             primary->id == static_cast<uint32_t>(display_id);
  if (primary)
    OH_NativeDisplayManager_DestroyDisplay(primary);
  const auto main = Application::GetInstance().GetPrimaryWindow();
  const bool focused = main && main->IsFocused();
  std::ostringstream s;
  s.precision(17);
  s << "{\"code\":" << code << ",\"afterCode\":" << after_code << ",\"workerCode\":" << worker_code
    << ",\"display\":" << display_id << ",\"afterDisplay\":" << after_id
    << ",\"workerDisplay\":" << worker_id << ",\"sdk\":[" << x << ',' << y << "],\"after\":["
    << after_x << ',' << after_y << "],\"workerSdk\":[" << worker_x << ',' << worker_y
    << "],\"cpp\":[" << cpp.x << ',' << cpp.y << "],\"c\":[" << c.x << ',' << c.y
    << "],\"workerCpp\":[" << worker_cpp.x << ',' << worker_cpp.y << "],\"workerC\":[" << worker_c.x
    << ',' << worker_c.y << "],\"event\":[" << event_x << ',' << event_y << "],\"scale\":" << scale
    << ",\"focused\":" << (focused ? "true" : "false")
    << ",\"primary\":" << (primary_match ? "true" : "false") << '}';
  const auto result = s.str();
  OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-cursor",
               "NATIVEAPI_CURSOR_SAMPLE %{public}s %{public}s", phase.c_str(), result.c_str());
  napi_value output;
  napi_create_string_utf8(env, result.c_str(), result.size(), &output);
  return output;
}
}  // namespace nativeapi::tests
