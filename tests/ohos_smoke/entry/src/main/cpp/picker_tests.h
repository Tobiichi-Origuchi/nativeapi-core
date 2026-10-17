#pragma once
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <set>
#include <sstream>
#include <thread>
#include "../../../../../../src/capi/file_dialog_c.h"
#include "../../../../../../src/file_dialog.h"
#include "../../../../../../src/foundation/handle_table.h"
#include "../../../../../../src/platform/ohos/ohos_napi.h"

namespace nativeapi::tests {
class PickerTests : public std::enable_shared_from_this<PickerTests> {
 public:
  napi_env env = nullptr;
  napi_deferred deferred = nullptr;
  uv_timer_t timer{};
  std::shared_ptr<FileDialog> picker;
  std::atomic<bool> completed{false};
  bool opened = false, finished = false, checked_pending = false, comma_rejected = false;
  bool cancel_only = false;
  int passed = 0, failed = 0, stage = -1, ticks = 0;
  native_file_dialog_t c_picker = NATIVE_INVALID_FILE_DIALOG;
  std::string name;
  const std::string run_token =
      std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
  std::vector<std::string> saved;
  std::ostringstream report;
  void Check(bool ok, const char* text) {
    (ok ? passed : failed)++;
    report << (ok ? "PASS " : "FAIL ") << text << '\n';
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 0x0001, "nativeapi-picker",
                 "%{public}s %{public}s", ok ? "PASS" : "FAIL", text);
  }
  void Start() {
    Check(FileDialog::IsSupported(), "system document picker module available");
    FileDialog invalid(static_cast<FileDialogMode>(99));
    Check(!invalid.Open() && invalid.GetResult() == FileDialogResult::Failed &&
              !invalid.GetLastError().empty(),
          "invalid picker mode fails explicitly");
    FileDialog wrong(FileDialogMode::OpenFile);
    wrong.SetModality(DialogModality::None);
    Check(!wrong.Open() && wrong.GetResult() == FileDialogResult::Failed,
          "non-window picker modality is rejected");
    FileDialog ui(FileDialogMode::SaveFile);
    Check(!ui.Open() && ui.GetResult() == FileDialogResult::Failed && !ui.GetLastError().empty(),
          "picker rejects blocking Open on the ArkTS UI thread");
    Check(!ui.SetFileTypes({"*"}) && !ui.SetFileTypes({std::string(".txt\0bad", 8)}) &&
              !ui.SetSuggestedFileName(std::string("bad\0name", 8)),
          "picker rejects unsafe type/name configuration");
    picker = std::make_shared<FileDialog>(FileDialogMode::OpenFile);
    picker->SetParentWindow(std::make_shared<Window>());
    auto self = shared_from_this();
    std::thread([self, current = picker] {
      FileDialog comma(FileDialogMode::OpenFile);
      comma.SetFileTypes({".a,b"});
      self->comma_rejected = !comma.Open() && comma.GetResult() == FileDialogResult::Failed &&
                             comma.GetLastError().find("comma") != std::string::npos;
      self->opened = current->Open();
      self->completed = true;
    }).detach();
  }
  std::string Name(char suffix) const {
    return "NativeAPI-picker-" + std::to_string(getpid()) + "-" + run_token + "-" + suffix +
           (suffix == 'b' ? " 中文 #%.md" : " 中文 #%.txt");
  }
  bool Document(const std::string& uri, const std::string& expected, bool writing) {
    bool ok = false;
    ohos::WithEnvironment([&](napi_env env) {
      napi_value fs = nullptr, modes = nullptr, mode = nullptr, path = nullptr, file = nullptr,
                 descriptor = nullptr;
      if (!ohos::js::Module(env, "@ohos.file.fs", fs) ||
          !ohos::js::Property(env, fs, "OpenMode", modes) ||
          !ohos::js::Property(env, modes, writing ? "READ_WRITE" : "READ_ONLY", mode) ||
          napi_create_string_utf8(env, uri.data(), uri.size(), &path) != napi_ok ||
          !ohos::js::Call(env, fs, "openSync", {path, mode}, file))
        return;
      int fd = -1;
      if (ohos::js::Property(env, file, "fd", descriptor) &&
          napi_get_value_int32(env, descriptor, &fd) == napi_ok && fd >= 0) {
        struct stat before{};
        ok = fstat(fd, &before) == 0;
        if (writing)
          ok = ok && before.st_size == 0 &&
               write(fd, expected.data(), expected.size()) == static_cast<ssize_t>(expected.size());
        char buffer[256]{};
        ok = ok && lseek(fd, 0, SEEK_SET) == 0;
        const auto length = ok ? read(fd, buffer, sizeof(buffer)) : -1;
        ok = length >= 0 && std::string(buffer, static_cast<size_t>(length)) == expected;
      }
      napi_value ignored = nullptr;
      const bool closed = ohos::js::Call(env, fs, "closeSync", {file}, ignored);
      ok = ok && closed;
    });
    return ok;
  }
  void BeginStage() {
    static const char* labels[] = {"save-a", "save-b", "open-one", "open-many",
                                   "folder", "cancel", "c-cancel"};
    const FileDialogMode modes[] = {FileDialogMode::SaveFile,     FileDialogMode::SaveFile,
                                    FileDialogMode::OpenFile,     FileDialogMode::OpenFiles,
                                    FileDialogMode::SelectFolder, FileDialogMode::OpenFile,
                                    FileDialogMode::OpenFile};
    name = Name(stage == 1 ? 'b' : 'a');
    ticks = 0;
    checked_pending = false;
    completed = false;
    if (stage == 6) {
      picker.reset();
      c_picker = native_file_dialog_create(NATIVE_FILE_DIALOG_MODE_OPEN_FILE);
      auto owner = native_window_manager_get_current();
      auto types = to_c_string_list({".txt"});
      Check(c_picker && native_file_dialog_is_supported() &&
                native_file_dialog_set_parent_window(c_picker, owner) &&
                native_file_dialog_set_file_types(c_picker, types) &&
                native_file_dialog_set_suggested_file_name(c_picker, name.c_str()),
            "generated C ABI configures the system picker");
      native_string_list_free(&types);
      native_window_free(owner);
      native_file_dialog_set_modality(c_picker, NATIVE_DIALOG_MODALITY_WINDOW);
      Check(native_file_dialog_get_modality(c_picker) == NATIVE_DIALOG_MODALITY_WINDOW,
            "generated C ABI preserves window modality");
    } else {
      picker = std::make_shared<FileDialog>(modes[stage]);
      Check(picker->SetParentWindow(WindowManager::GetInstance().GetCurrent()) &&
                picker->SetFileTypes(stage == 1
                                         ? std::vector<std::string>{".md"}
                                         : (stage == 3 ? std::vector<std::string>{".txt", ".md"}
                                                       : std::vector<std::string>{".txt"})) &&
                picker->SetSuggestedFileName(name),
            "configure system picker parent, filter and Unicode prefilled name");
    }
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0001, "nativeapi-picker",
                 "NATIVEAPI_PICKER_BEGIN %{public}s %{public}s", labels[stage], name.c_str());
    auto self = shared_from_this();
    std::thread([self, current = picker] {
      self->opened = current ? current->Open() : native_file_dialog_open(self->c_picker);
      self->completed = true;
    }).detach();
  }
  void Tick() {
    if (finished)
      return;
    if (++ticks > 1800) {
      Check(false, "native picker interaction deadline");
      Finish();
      return;
    }
    if (!completed) {
      if (stage >= 0 && ticks >= 5 && !checked_pending) {
        checked_pending = true;
        if (picker) {
          Check(!picker->SetSuggestedFileName("changed.txt") && !picker->SetFileTypes({".png"}) &&
                    !picker->SetParentWindow(nullptr) && !picker->Open(),
                "pending picker configuration is immutable and duplicate Open is rejected");
          Check(picker->GetResult() == FileDialogResult::None && picker->GetPaths().empty() &&
                    picker->GetLastError().empty(),
                "pending picker exposes no partial result");
          Check(!picker->Close(), "unsupported programmatic close leaves system picker pending");
        } else {
          Check(!native_file_dialog_open(c_picker) && !native_file_dialog_close(c_picker) &&
                    !native_file_dialog_set_suggested_file_name(c_picker, "changed.txt") &&
                    native_file_dialog_get_result(c_picker) == NATIVE_FILE_DIALOG_RESULT_NONE,
                "generated C ABI respects pending native picker state");
        }
      }
      return;
    }
    if (stage == -1) {
      Check(comma_rejected,
            "unrepresentable literal-comma extension fails explicitly before native UI");
      Check(!opened && picker->GetResult() == FileDialogResult::Failed &&
                !picker->GetLastError().empty(),
            "worker picker rejects an unattached parent without opening system UI");
    } else if (stage == 6) {
      auto paths = native_file_dialog_get_paths(c_picker);
      char* error = native_file_dialog_get_last_error(c_picker);
      Check(opened &&
                native_file_dialog_get_result(c_picker) == NATIVE_FILE_DIALOG_RESULT_CANCELLED &&
                paths.count == 0 && (!error || std::string(error).empty()),
            "generated C ABI observes native user cancellation");
      native_string_list_free(&paths);
      free_c_str(error);
      native_file_dialog_free(c_picker);
      native_file_dialog_free(c_picker);
      Check(
          !native_file_dialog_open(c_picker) && !native_file_dialog_set_parent_window(c_picker, 0),
          "released C ABI picker handles fail safely");
      c_picker = 0;
    } else {
      auto paths = picker->GetPaths();
      const auto error = picker->GetLastError();
      OH_LOG_Print(LOG_APP, LOG_INFO, 0x0001, "nativeapi-picker",
                   "PICKER_RESULT stage=%{public}d opened=%{public}d result=%{public}d "
                   "count=%{public}zu error=%{public}s",
                   stage, opened, static_cast<int>(picker->GetResult()), paths.size(),
                   error.c_str());
      if (stage == 5) {
        Check(opened && picker->GetResult() == FileDialogResult::Cancelled && paths.empty() &&
                  error.empty(),
              "native user cancellation returns success with no selected documents");
      } else {
        Check(opened && picker->GetResult() == FileDialogResult::Accepted &&
                  paths.size() == (stage == 3 ? 2 : 1) && error.empty(),
              "system picker returns accepted authorized document URIs");
        auto handle = HandleTable::GetInstance().Insert(picker);
        auto c_paths = native_file_dialog_get_paths(handle);
        bool c_matches = c_paths.count == static_cast<long>(paths.size());
        for (long i = 0; c_matches && i < c_paths.count; ++i)
          c_matches = c_paths.items[i] && paths[i] == c_paths.items[i];
        Check(c_matches &&
                  native_file_dialog_get_result(handle) == NATIVE_FILE_DIALOG_RESULT_ACCEPTED,
              "generated C ABI preserves every selected document URI");
        native_string_list_free(&c_paths);
        native_file_dialog_free(handle);
        if (stage <= 1 && paths.size() == 1) {
          Check(paths[0].rfind("file://", 0) == 0 &&
                    Document(paths[0], stage == 0 ? "NativeAPI A 中文🙂" : "NativeAPI B 中文🙂",
                             true),
                "save URI authorizes writing and reading actual document bytes");
          saved.push_back(paths[0]);
        } else if (stage == 2 && paths.size() == 1 && !saved.empty()) {
          Check(paths[0] == saved[0] && Document(paths[0], "NativeAPI A 中文🙂", false),
                "single-open returns the same authorized document and original contents");
        } else if (stage == 3 && saved.size() == 2 && paths.size() == 2) {
          bool contents = true;
          for (const auto& uri : paths)
            contents &=
                Document(uri, uri == saved[0] ? "NativeAPI A 中文🙂" : "NativeAPI B 中文🙂", false);
          Check(std::set<std::string>(paths.begin(), paths.end()) ==
                        std::set<std::string>(saved.begin(), saved.end()) &&
                    contents,
                "multiple-open preserves both actual documents and contents");
        } else if (stage == 4 && paths.size() == 1) {
          const auto expected =
              saved.empty() ? std::string{} : saved[0].substr(0, saved[0].find_last_of('/'));
          Check(!expected.empty() && (paths[0] == expected || paths[0] == expected + "/"),
                "folder picker returns the selected authorized directory URI");
        }
      }
    }
    if (failed || ++stage > 6)
      Finish();
    else
      BeginStage();
  }
  void Finish() {
    if (finished)
      return;
    finished = true;
    std::ostringstream summary;
    summary << (cancel_only ? "NATIVEAPI_PICKER_CANCEL_" : "NATIVEAPI_PICKER_")
            << (failed ? "FAIL" : "PASS") << " (" << passed
            << " passed, " << failed << " failed)\n";
    report << summary.str();
    OH_LOG_Print(LOG_APP, failed ? LOG_ERROR : LOG_INFO, 0x0001, "nativeapi-picker", "%{public}s",
                 summary.str().c_str());
    napi_handle_scope scope;
    napi_open_handle_scope(env, &scope);
    napi_value result;
    const auto text = report.str();
    napi_create_string_utf8(env, text.data(), text.size(), &result);
    napi_resolve_deferred(env, deferred, result);
    napi_close_handle_scope(env, scope);
    uv_timer_stop(&timer);
    uv_close(reinterpret_cast<uv_handle_t*>(&timer), [](uv_handle_t* handle) {
      delete static_cast<std::shared_ptr<PickerTests>*>(handle->data);
    });
  }
};
inline napi_value RunPickers(napi_env env, napi_callback_info info) {
  auto tests = std::make_shared<PickerTests>();
  size_t count = 1;
  napi_value argument = nullptr;
  napi_get_cb_info(env, info, &count, &argument, nullptr, nullptr);
  if (count)
    napi_get_value_bool(env, argument, &tests->cancel_only);
  tests->env = env;
  napi_value promise;
  napi_create_promise(env, &tests->deferred, &promise);
  uv_loop_t* loop = nullptr;
  napi_get_uv_event_loop(env, &loop);
  uv_timer_init(loop, &tests->timer);
  tests->timer.data = new std::shared_ptr<PickerTests>(tests);
  uv_timer_start(
      &tests->timer,
      [](uv_timer_t* timer) { (*static_cast<std::shared_ptr<PickerTests>*>(timer->data))->Tick(); },
      100, 100);
  if (tests->cancel_only) {
    tests->stage = 5;
    tests->BeginStage();
  } else {
    tests->Start();
  }
  return promise;
}
}  // namespace nativeapi::tests
