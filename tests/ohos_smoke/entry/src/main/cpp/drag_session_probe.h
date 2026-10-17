#pragma once
#include <accesstoken/ability_access_control.h>
#include <multimodalinput/oh_input_manager.h>
#include "../../../../../../src/window_drag_session.h"
#include "../../../../../../src/capi/window_drag_session_c.h"

namespace nativeapi::tests {
inline void DragMouseProbe(const Input_MouseEvent*) {}
inline napi_value DragSessionProbe(napi_env env, napi_callback_info) {
  const bool granted = OH_AT_CheckSelfPermission("ohos.permission.INPUT_MONITORING");
  const auto monitor = OH_Input_AddMouseEventMonitor(DragMouseProbe);
  int removed = -1;
  if (monitor == INPUT_SUCCESS) removed = OH_Input_RemoveMouseEventMonitor(DragMouseProbe);
  const auto primary = Application::GetInstance().GetPrimaryWindow();
  const auto alias = primary ? native_window_manager_get(primary->GetId()) : 0;
  std::array<bool, 4> starts{};
  WindowDragSession session;
  starts[0] = session.Start(nullptr, {});
  starts[1] = session.Start(primary, {20, 20});
  const auto c = native_window_drag_session_create();
  starts[2] = native_window_drag_session_start(c, 0, {0, 0});
  starts[3] = native_window_drag_session_start(c, alias, {20, 20});
  std::ostringstream output;
  output << "{\"permission\":" << (granted ? "true" : "false")
         << ",\"monitor\":" << int(monitor) << ",\"removed\":" << removed
         << ",\"starts\":[";
  for (size_t i = 0; i < starts.size(); ++i) {
    if (i) output << ',';
    output << (starts[i] ? "true" : "false");
  }
  output << "],\"active\":[" << (session.IsActive() ? "true" : "false") << ','
         << (native_window_drag_session_is_active(c) ? "true" : "false") << "]}";
  session.Cancel(); native_window_drag_session_cancel(c);
  native_window_drag_session_free(c); native_window_free(alias);
  return ohos::String(env, output.str());
}
}  // namespace nativeapi::tests
