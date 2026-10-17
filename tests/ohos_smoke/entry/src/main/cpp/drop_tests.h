#pragma once
#include "../../../../../../src/capi/drop_target_c.h"
#include "../../../../../../src/drop_target.h"
namespace nativeapi::tests {
struct DropJournal {
  int passed = 0, failed = 0, entered = 0, moved = 0, exited = 0, dropped = 0, c_events = 0;
  bool remove_on_enter = false, files = false, files_only = false;
  std::shared_ptr<DropTarget> target;
  size_t listener = 0;
  native_drop_target_t c_target = 0;
  size_t c_listener = 0;
  Point position{};
  bool Files(const std::vector<std::string>& paths) {
    if (!files)
      return paths.empty();
    OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-drop", "NATIVEAPI_DROP_FILES count=%{public}zu",
                 paths.size());
    if (paths.size() != 2 || paths[0] == paths[1])
      return false;
    for (int i = 0; i < 2; ++i) {
      std::ifstream file(paths[i], std::ios::binary);
      std::string bytes((std::istreambuf_iterator<char>(file)), {});
      OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-drop",
                   "NATIVEAPI_DROP_FILE path=%{public}s bytes=%{public}s", paths[i].c_str(),
                   bytes.c_str());
      if (bytes != "NativeAPI SDK file " + std::to_string(i) + " 中文 +#%")
        return false;
    }
    return true;
  }
  void Check(bool ok, const char* text) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-drop", "%{public}s %{public}s",
                 ok ? "PASS" : "FAIL", text);
  }
};
inline DropJournal drop_journal;
inline napi_value DropState(napi_env env, napi_callback_info info) {
  napi_value args[2]{};
  size_t count = 2;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  auto command = count ? ohos::js::String(env, args[0]) : std::string();
  auto request = count > 1 ? ohos::js::String(env, args[1]) : std::string();
  auto& j = drop_journal;
  auto window = WindowManager::GetInstance().GetCurrent();
  if (command == "setup") {
    j.Check(DropTarget::IsSupported() && native_drop_target_is_supported(),
            "native ArkUI destination service available through C++ and C");
    auto root = window->GetContentView();
    j.Check(root != nullptr, "destination has an attached NativeAPI content root");
    if (root)
      root->SetBackgroundColor(Color::Blue);
    auto invalid = std::make_shared<DropTarget>(nullptr);
    invalid->AddListener<DropTargetEvent>([](const auto&) {});
    j.Check(!invalid->IsActive() && invalid->GetWindowId() == 0,
            "missing window cannot activate a destination");
    j.target = std::make_shared<DropTarget>(window);
    j.Check(!j.target->IsActive() && j.target->GetWindowId() == window->GetId() &&
                j.target->GetDropOperation() == DragOperation::Copy,
            "destination starts inactive with stable window identity and Copy operation");
  }
  if (command == "listen") {
    j.listener = j.target->AddListener<DropTargetEvent>([](const auto& event) {
      auto& j = drop_journal;
      j.Check(IsMainThread(), "native drag events delivered on UI thread");
      j.Check(event.GetWindowId() == WindowManager::GetInstance().GetCurrent()->GetId(),
              "native event preserves actual window identity");
      j.position = event.GetPosition();
      j.Check(j.position.x >= 0 && j.position.y >= 0,
              "event coordinates lie in logical content space");
      if (dynamic_cast<const DropTargetEnteredEvent*>(&event)) {
        ++j.entered;
        if (j.remove_on_enter) {
          j.target.reset();
          j.Check(true, "target may be destroyed from its enter callback");
        }
      }
      if (dynamic_cast<const DropTargetMovedEvent*>(&event))
        ++j.moved;
      if (dynamic_cast<const DropTargetExitedEvent*>(&event))
        ++j.exited;
      if (auto* drop = dynamic_cast<const DropTargetDroppedEvent*>(&event)) {
        ++j.dropped;
        j.Check(drop->GetText() == (j.files_only ? "" : "NativeAPI drag 中文 🙂") &&
                    j.Files(drop->GetFilePaths()),
                "actual SDK drag delivers exact Unicode text and requested file bytes");
      }
    });
    j.Check(j.target->IsActive(), "first listener registers an actual native drop destination");
    auto duplicate = std::make_shared<DropTarget>(window);
    duplicate->AddListener<DropTargetEvent>([](const auto&) {});
    j.Check(!duplicate->IsActive(), "second target cannot replace the first root registration");
  }
  if (command == "none" || command == "copy" || command == "move" || command == "link") {
    const auto operation = command == "copy"   ? DragOperation::Copy
                           : command == "move" ? DragOperation::Move
                           : command == "link" ? DragOperation::Link
                                               : DragOperation::None;
    j.target->SetDropOperation(operation);
    j.Check(j.target->GetDropOperation() == operation,
            "drop operation setter/getter retains requested value");
  }
  if (command == "unlisten") {
    j.Check(j.target->RemoveListener(j.listener) && !j.target->IsActive(),
            "removing last listener unregisters native destination");
    j.Check(!j.target->RemoveListener(j.listener), "repeated listener removal returns false");
  }
  if (command == "files" || command == "files-only" || command == "text") {
    j.files = command != "text";
    j.files_only = command == "files-only";
  }
  if (command == "self-remove")
    j.remove_on_enter = true;
  if (command == "c-listen") {
    auto alias = native_window_manager_get_current();
    j.c_target = native_drop_target_create(alias);
    native_window_free(alias);
    j.Check(j.c_target != 0 && !native_drop_target_is_active(j.c_target),
            "C destination starts inactive");
    j.c_listener = native_drop_target_add_listener(
        j.c_target,
        [](const native_drop_target_event_t* event, void*) {
          auto& j = drop_journal;
          ++j.c_events;
          j.Check(IsMainThread() &&
                      event->window_id == WindowManager::GetInstance().GetCurrent()->GetId(),
                  "C event preserves UI thread and window identity");
          if (event->type == NATIVE_DROP_TARGET_EVENT_TYPE_DROPPED) {
            ++j.dropped;
            std::vector<std::string> paths;
            for (size_t i = 0; i < event->data.dropped.file_paths.count; ++i)
              paths.emplace_back(event->data.dropped.file_paths.items[i]);
            j.Check(j.Files(paths), "C file paths resolve to exact SDK source bytes");
            j.Check(std::string(event->data.dropped.text ? event->data.dropped.text : "") ==
                        (j.files_only ? "" : "NativeAPI drag 中文 🙂"),
                    "C dropped payload preserves exact SDK text");
          }
        },
        nullptr, nullptr);
    j.Check(native_drop_target_is_active(j.c_target),
            "C listener registers native destination after previous owner released it");
  }
  if (command == "c-remove") {
    j.Check(native_drop_target_remove_listener(j.c_target, j.c_listener) &&
                !native_drop_target_is_active(j.c_target),
            "C last listener removal deactivates native destination");
    native_drop_target_free(j.c_target);
    j.c_target = 0;
  }
  auto state = ohos::FindWindowState(window->GetNativeObject());
  const auto content = ohos::WindowBounds(state, true);
  std::ostringstream out;
  out << "{\"request\":\"" << request << "\",\"passed\":" << j.passed << ",\"failed\":" << j.failed
      << ",\"entered\":" << j.entered << ",\"moved\":" << j.moved << ",\"exited\":" << j.exited
      << ",\"dropped\":" << j.dropped << ",\"cEvents\":" << j.c_events
      << ",\"scale\":" << ohos::WindowScale(state) << ",\"contentX\":" << content.x
      << ",\"contentY\":" << content.y << ",\"x\":" << j.position.x << ",\"y\":" << j.position.y
      << "}";
  OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-drop", "NATIVEAPI_DROP_REPLY %{public}s",
               out.str().c_str());
  return ohos::String(env, out.str());
}
}  // namespace nativeapi::tests
