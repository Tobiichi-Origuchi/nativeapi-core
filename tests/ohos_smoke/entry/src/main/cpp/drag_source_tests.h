#pragma once
#include <arkui/drag_and_drop.h>
#include <database/udmf/udmf_err_code.h>
#include <database/udmf/uds.h>
#include "../../../../../../src/capi/drag_source_c.h"
#include "../../../../../../src/drag_source.h"
#include "../../../../../../src/platform/ohos/ohos_arkui.h"
namespace nativeapi::tests {
struct SourceJournal {
  int passed = 0, failed = 0, starts = 0, ended = 0, operation = 0, workers = 0;
  bool armed = false, use_c = false, last_return = false, destroy_live = false, destroy_end = false,
       snapshot = false;
  std::string scene;
  ArkUI_DragAction* reference = nullptr;
  std::shared_ptr<Image> reference_image;
  std::unique_ptr<OH_UdmfData, decltype(&OH_UdmfData_Destroy)> reference_data{nullptr,
                                                                              OH_UdmfData_Destroy};
  int reference_ended = 0;
  double reference_x = -1, reference_y = -1;
  void Reference() {
    auto window = WindowManager::GetInstance().GetCurrent();
    ohos::WithUiContext(window, [&] {
      auto node = ohos::NodeApi()->createNode(ARKUI_NODE_STACK);
      reference = OH_ArkUI_CreateDragActionWithNode(node);
      ohos::NodeApi()->disposeNode(node);
    });
    Check(reference != nullptr, "independent direct SDK source obtains its own action");
    if (!reference)
      return;
    reference_data.reset(OH_UdmfData_Create());
    auto* record = OH_UdmfRecord_Create();
    auto* text = OH_UdsPlainText_Create();
    OH_UdsPlainText_SetContent(text, "NativeAPI source 中文 🙂");
    OH_UdmfRecord_AddPlainText(record, text);
    OH_UdmfData_AddRecord(reference_data.get(), record);
    OH_UdsPlainText_Destroy(text);
    OH_UdmfRecord_Destroy(record);
    reference_image = Image::FromBase64(FixturePng);
    auto* pixels = static_cast<OH_PixelmapNative*>(reference_image->GetNativeObject());
    OH_ArkUI_DragAction_SetData(reference, reference_data.get());
    OH_ArkUI_DragAction_SetPointerId(reference, 0);
    OH_ArkUI_DragAction_SetPixelMaps(reference, &pixels, 1);
    OH_ArkUI_DragAction_RegisterStatusListener(
        reference, this, [](ArkUI_DragAndDropInfo* info, void* data) {
          if (OH_ArkUI_DragAndDropInfo_GetDragStatus(info) != ARKUI_DRAG_STATUS_ENDED)
            return;
          auto* event = OH_ArkUI_DragAndDropInfo_GetDragEvent(info);
          const auto x = OH_ArkUI_DragEvent_GetTouchPointXToDisplay(event);
          const auto y = OH_ArkUI_DragEvent_GetTouchPointYToDisplay(event);
          ArkUI_DragResult result{};
          OH_ArkUI_DragEvent_GetDragResult(event, &result);
          const bool worker = !IsMainThread();
          ohos::Post([j = static_cast<SourceJournal*>(data), x, y, result, worker] {
            ++j->reference_ended;
            j->reference_x = x;
            j->reference_y = y;
            j->Check(result == ARKUI_DRAG_RESULT_SUCCESSFUL && worker,
                     "independent SDK confirms successful completion on its worker thread");
            j->Check(
                x == 0 && y == 0,
                "independent SDK end callback has no coordinates despite a non-origin destination");
            OH_ArkUI_DragAction_UnregisterStatusListener(j->reference);
            OH_ArkUI_DragAction_Dispose(j->reference);
            j->reference = nullptr;
            j->reference_data.reset();
            j->reference_image.reset();
            OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-source",
                         "NATIVEAPI_SOURCE_REFERENCE_END %{public}d x=%{public}f y=%{public}f",
                         j->reference_ended, x, y);
          });
        });
    Check(OH_ArkUI_StartDrag(reference) == 0, "independent SDK source submits a real action");
  }
  std::vector<std::string> files;
  std::shared_ptr<DragSource> source;
  native_drag_source_t c_source = 0;
  void Check(bool ok, const char* text) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-source", "%{public}s %{public}s",
                 ok ? "PASS" : "FAIL", text);
  }
  void Clear() {
    source.reset();
    native_drag_source_free(c_source);
    c_source = 0;
  }
  void End(WindowId id, Point position, DragOperation result) {
    ++ended;
    operation = static_cast<int>(result);
    Check(IsMainThread(), "SDK worker completion is delivered on UI");
    Check(id == WindowManager::GetInstance().GetCurrent()->GetId(),
          "completion preserves source window identity");
    Check(!DragSource::IsPositionSupported() && position.x == 0 && position.y == 0,
          "missing SDK end position is explicitly unavailable with origin fallback");
    Check(use_c ? !native_drag_source_is_dragging(c_source) : source && !source->IsDragging(),
          "source is no longer dragging before completion listeners run");
    const auto expected =
        scene.find("target-move") != std::string::npos ? DragOperation::Move
        : scene.find("refuse") != std::string::npos || scene.find("leave") != std::string::npos
            ? DragOperation::None
            : DragOperation::Copy;
    Check(result == expected, "completion reflects actual SDK destination result");
    if (destroy_end)
      Clear();
    OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-source", "NATIVEAPI_SOURCE_ENDED %{public}d",
                 ended);
  }
};
inline SourceJournal source_journal;
inline napi_value SourceState(napi_env env, napi_callback_info info) {
  napi_value args[2]{};
  size_t count = 2;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  auto command = count ? ohos::js::String(env, args[0]) : std::string();
  auto request = count > 1 ? ohos::js::String(env, args[1]) : std::string();
  auto& j = source_journal;
  auto window = WindowManager::GetInstance().GetCurrent();
  if (command == "setup") {
    j.Check(DragSource::IsSupported() && native_drag_source_is_supported(),
            "C++ and C report native drag-source support");
    j.Check(!DragSource::IsOperationSelectionSupported() &&
                !native_drag_source_is_operation_selection_supported(),
            "C++ and C expose the missing native source-offer mask");
    j.Check(!DragSource::IsPositionSupported() && !native_drag_source_is_position_supported(),
            "C++ and C expose the SDK position limitation independently");
    j.Check(!window->GetContentView(),
            "programmatic source supports ArkTS-only window content without a native content root");
    ohos::WithContext([&](napi_env e, napi_value context) {
      auto dir = ohos::js::StringProperty(e, context, "filesDir");
      for (int i = 0; i < 2; ++i) {
        auto path = dir + "/source 中文 +#% " + std::to_string(i) + ".txt";
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "NativeAPI native file " << i << " 中文 +#%";
        j.Check(file.good(), "fixture creates a real SDK-readable source file");
        j.files.push_back(path);
      }
    });
  }
  if (command.rfind("cpp-", 0) == 0 || command.rfind("c-", 0) == 0) {
    j.Clear();
    j.scene = command;
    j.use_c = command.rfind("c-", 0) == 0;
    j.destroy_live = command.find("destroy-live") != std::string::npos;
    j.destroy_end = command.find("destroy-end") != std::string::npos;
    j.snapshot = command.find("snapshot") != std::string::npos;
    const bool files =
        command.find("files") != std::string::npos || command.find("mixed") != std::string::npos;
    const std::optional<std::string> text =
        command.find("files") != std::string::npos || command.find("empty") != std::string::npos
            ? std::nullopt
            : std::optional<std::string>("NativeAPI source 中文 🙂");
    const auto operation = (command == "cpp-move" || command == "c-move") ? DragOperation::Move
                           : command.find("link") != std::string::npos    ? DragOperation::Link
                           : command.find("none") != std::string::npos    ? DragOperation::None
                                                                          : DragOperation::Copy;
    if (j.use_c) {
      j.c_source = native_drag_source_create();
      native_drag_source_set_text(j.c_source, text ? text->c_str() : nullptr);
      auto list = to_c_string_list(files ? j.files : std::vector<std::string>{});
      native_drag_source_set_file_paths(j.c_source, list);
      native_string_list_free(&list);
      list = native_drag_source_get_file_paths(j.c_source);
      bool paths_ok = list.count == (files ? j.files.size() : 0);
      for (long i = 0; paths_ok && i < list.count; ++i)
        paths_ok = list.items[i] == j.files[i];
      j.Check(paths_ok, "C source copies the complete file-path list");
      native_string_list_free(&list);
      if (j.snapshot) {
        auto image = native_image_from_base64(TrayMagenta);
        native_drag_source_set_image(j.c_source, image);
        native_image_free(image);
        image = native_drag_source_get_image(j.c_source);
        j.Check(image && native_image_get_size(image).width == 24,
                "C preview survives release of the caller image handle");
        native_image_free(image);
      }
      native_drag_source_set_drag_operation(j.c_source,
                                            static_cast<native_drag_operation_t>(operation));
      native_drag_source_add_listener(
          j.c_source,
          [](const native_drag_source_event_t* event, void*) {
            source_journal.End(event->window_id, {event->position.x, event->position.y},
                               static_cast<DragOperation>(event->data.ended.operation));
          },
          nullptr, nullptr);
      auto returned = native_drag_source_get_text(j.c_source);
      j.Check((returned ? std::optional<std::string>(returned) : std::nullopt) == text,
              "C source copies optional text");
      free_c_str(returned);
      j.Check(!native_drag_source_is_dragging(j.c_source) &&
                  native_drag_source_get_drag_operation(j.c_source) ==
                      static_cast<native_drag_operation_t>(operation),
              "C source starts idle with requested operation");
    } else {
      j.source = std::make_shared<DragSource>();
      j.source->SetText(text);
      j.source->SetFilePaths(files ? j.files : std::vector<std::string>{});
      j.source->SetDragOperation(operation);
      if (j.snapshot)
        j.source->SetImage(Image::FromBase64(TrayMagenta));
      if (command.find("invalid-file") != std::string::npos)
        j.source->SetFilePaths({"/definitely/not/a/nativeapi/file.txt"});
      if (command.find("nul") != std::string::npos)
        j.source->SetText(std::string("bad\0payload", 11));
      j.source->AddListener<DragSourceEndedEvent>([](const auto& event) {
        source_journal.End(event.GetWindowId(), event.GetPosition(), event.GetOperation());
      });
      j.Check(!j.source->IsDragging() &&
                  j.source->GetText() ==
                      (command.find("nul") != std::string::npos
                           ? std::optional<std::string>(std::string("bad\0payload", 11))
                           : text) &&
                  j.source->GetDragOperation() == operation,
              "C++ source properties are copied before starting");
      j.Check(j.snapshot ? j.source->GetImage() != nullptr : j.source->GetImage() == nullptr,
              "custom/default preview getter preserves requested image");
    }
    j.armed = true;
  }
  if (command == "sdk-reference") {
    j.Clear();
    j.scene = command;
    j.armed = true;
    j.use_c = false;
  }
  if (command == "idle") {
    const bool result = j.source->StartDragging(window);
    j.Check(!result && !j.source->IsDragging(),
            "SDK refusal without a pressed pointer returns false and leaves no active drag");
  }
  std::ostringstream out;
  out << "{\"request\":\"" << request << "\",\"passed\":" << j.passed << ",\"failed\":" << j.failed
      << ",\"workers\":" << j.workers << ",\"starts\":" << j.starts << ",\"ended\":" << j.ended
      << ",\"operation\":" << j.operation
      << ",\"lastReturn\":" << (j.last_return ? "true" : "false") << ",\"dragging\":"
      << ((j.use_c ? native_drag_source_is_dragging(j.c_source)
                   : j.source && j.source->IsDragging())
              ? "true"
              : "false")
      << "}";
  OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-source", "NATIVEAPI_SOURCE_REPLY %{public}s",
               out.str().c_str());
  return ohos::String(env, out.str());
}
inline napi_value SourceBegin(napi_env env, napi_callback_info) {
  auto& j = source_journal;
  if (!j.armed)
    return ohos::Boolean(env, false);
  j.armed = false;
  if (j.scene == "sdk-reference") {
    j.Reference();
    return ohos::Boolean(env, true);
  }
  ++j.starts;
  auto window = WindowManager::GetInstance().GetCurrent();
  if (j.use_c) {
    auto alias = native_window_manager_get_current();
    j.last_return = native_drag_source_start_dragging(j.c_source, alias);
    native_window_free(alias);
  } else
    j.last_return = j.source->StartDragging(window);
  if (j.last_return) {
    j.Check(j.use_c ? native_drag_source_is_dragging(j.c_source) : j.source->IsDragging(),
            "successful native source is active after submission");
    if (!j.use_c)
      j.Check(!j.source->StartDragging(window), "one source cannot begin two concurrent drags");
    auto other = std::make_shared<DragSource>();
    other->SetText("competing source");
    j.Check(!other->StartDragging(window) && !other->IsDragging(),
            "another NativeAPI source cannot disturb an ongoing SDK drag");
    if (j.snapshot) {
      if (j.use_c) {
        native_drag_source_set_text(j.c_source, "changed after submit");
        native_drag_source_set_image(j.c_source, 0);
      } else {
        j.source->SetText("changed after submit");
        j.source->SetFilePaths({});
        j.source->SetImage(nullptr);
        j.Check(j.source->GetText() == std::optional<std::string>("changed after submit") &&
                    !j.source->GetImage(),
                "properties may change after the drag snapshot was submitted");
      }
    }
    if (j.destroy_live)
      j.Clear();
    if (j.scene.find("worker-destroy") != std::string::npos) {
      struct Work {
        uv_work_t work{};
        std::shared_ptr<DragSource> source;
        native_drag_source_t handle = 0;
      };
      auto* task = new Work;
      task->source = std::move(j.source);
      task->handle = j.c_source;
      j.c_source = 0;
      task->work.data = task;
      uv_loop_t* loop = nullptr;
      napi_get_uv_event_loop(env, &loop);
      const auto queued = uv_queue_work(
          loop, &task->work,
          [](uv_work_t* work) {
            auto* task = static_cast<Work*>(work->data);
            task->source.reset();
            native_drag_source_free(task->handle);
          },
          [](uv_work_t* work, int status) {
            delete static_cast<Work*>(work->data);
            auto& j = source_journal;
            ++j.workers;
            j.Check(status == 0,
                    "source destruction on a worker completes while SDK drag remains independent");
            OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-source",
                         "NATIVEAPI_SOURCE_WORKER_RELEASED %{public}d", j.workers);
          });
      j.Check(queued == 0, "worker source destruction was queued without blocking UI");
      if (queued != 0)
        delete task;
    }
  }
  OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-source",
               "NATIVEAPI_SOURCE_START %{public}d %{public}d", j.starts, j.last_return);
  return ohos::Boolean(env, j.last_return);
}
}  // namespace nativeapi::tests
