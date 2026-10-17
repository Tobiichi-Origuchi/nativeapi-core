#pragma once
#include <database/pasteboard/oh_pasteboard.h>
#include <database/pasteboard/oh_pasteboard_err_code.h>
#include "../../../../../../src/capi/common_c.h"
#include "../../../../../../src/capi/image_c.h"

namespace nativeapi::tests {
struct ClipboardBoundaryJournal {
  size_t cpp_listener = 0;
  native_listener_id_t c_listener = 0;
  int cpp = 0, c = 0, removed = 0, released = 0;
  std::atomic<int> references{0};
  OH_Pasteboard* board = nullptr;
  OH_PasteboardObserver* reference = nullptr;
  bool self_remove = false;
  int callbacks = 0, c_releases = 0, batches = 0;
  std::vector<std::string> foreign_files;
  void Check(bool ok, const char* label) { clipboard_interop.Check(ok, label); }
  void Listen() {
    self_remove = false;
    auto& b = Clipboard::GetInstance();
    if (!cpp_listener)
      cpp_listener = b.AddListener<ClipboardChangedEvent>([this](const auto&) {
        ++cpp;
        Check(IsMainThread(), "clipboard epoch C++ notice arrives on UI");
        if (self_remove) {
          Check(Clipboard::GetInstance().RemoveListener(cpp_listener),
                "C++ notice can remove its listener");
          cpp_listener = 0;
          ++removed;
        }
      });
    if (!c_listener)
      c_listener = native_clipboard_add_listener(
          [](const auto* event, void* data) {
            auto& j = *static_cast<ClipboardBoundaryJournal*>(data);
            ++j.c;
            j.Check(IsMainThread() && event->type == NATIVE_CLIPBOARD_EVENT_TYPE_CHANGED,
                    "clipboard epoch C notice has actual type on UI");
            if (j.self_remove) {
              j.Check(native_clipboard_remove_listener(j.c_listener),
                      "last C listener can remove itself from notice");
              j.c_listener = 0;
              ++j.removed;
            }
          },
          this,
          [](void* data) {
            auto& j = *static_cast<ClipboardBoundaryJournal*>(data);
            ++j.released;
            j.Check(IsMainThread(), "clipboard observer C user data releases on a later UI turn");
          });
    Check(cpp_listener && c_listener && b.IsMonitoring(),
          "clipboard new native observer epoch is active");
  }
  void Stop() {
    if (cpp_listener)
      Check(Clipboard::GetInstance().RemoveListener(cpp_listener),
            "clipboard epoch C++ listener removed");
    if (c_listener)
      Check(native_clipboard_remove_listener(c_listener), "clipboard epoch C listener removed");
    cpp_listener = c_listener = 0;
    Check(!Clipboard::GetInstance().IsMonitoring(),
          "last observer removal stops the native subscription");
  }
  size_t Held() {
    std::lock_guard<std::mutex> lock(display_test_dispatcher.mutex);
    return display_test_dispatcher.queued.size();
  }
  void Report(const std::string& command, const std::string& request) {
    std::ostringstream out;
    out << "{\"ok\":true,\"passed\":" << clipboard_interop.passed
        << ",\"failed\":" << clipboard_interop.failed << ",\"cpp\":" << cpp << ",\"c\":" << c
        << ",\"removed\":" << removed << ",\"released\":" << released
        << ",\"references\":" << references.load() << ",\"held\":" << Held()
        << ",\"monitoring\":" << int(Clipboard::GetInstance().IsMonitoring())
        << ",\"callbacks\":" << callbacks << ",\"cReleases\":" << c_releases
        << ",\"batches\":" << batches << '}';
    OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-clipboard",
                 "NATIVEAPI_CLIPBOARD_REPLY %{public}s %{public}s %{public}s", request.c_str(),
                 command.c_str(), out.str().c_str());
  }
};
inline ClipboardBoundaryJournal clipboard_boundary;
struct ClipboardReadBatch : std::enable_shared_from_this<ClipboardReadBatch> {
  std::string command, request;
  bool invalidated = false, denied = false;
  std::array<int, 30> seen{};
  std::array<std::atomic<bool>, 30> returned{};
  int completed = 0;
  void Complete(int index, bool ok, ClipboardData data) {
    auto& j = clipboard_boundary;
    const auto format = index % 5;
    j.Check(IsMainThread() && returned[index].load() && ++seen[index] == 1,
            "each concurrent clipboard read completes exactly once on a later UI turn");
    if (invalidated || denied) {
      j.Check(!ok && !data.text && !data.html && !data.image && data.file_paths.empty(),
              "failed clipboard read returns no partial format payload");
    } else {
      const bool text = format == 0 || format == 1, html = format == 0 || format == 2;
      const bool image = format == 0 || format == 3, files = format == 0 || format == 4;
      j.Check(
          ok &&
              data.text == (text ? std::optional<std::string>("peer-text-中文🙂") : std::nullopt) &&
              data.html ==
                  (html ? std::optional<std::string>("<b>peer-html-中文🙂</b>") : std::nullopt),
          "concurrent all/shortcut reads retain exact peer text and HTML and omit other formats");
      j.Check(bool(data.image) == image && data.file_paths.size() == (files ? 2 : 0),
              "concurrent reads retain only requested image and file formats");
      if (data.image) {
        std::array<uint8_t, 24> bytes{};
        size_t size = bytes.size();
        const auto expected =
            std::array<uint8_t, 24>{255, 0,   0,   255, 0, 255, 0, 255, 0,   0,   255, 255,
                                    255, 255, 255, 255, 0, 0,   0, 255, 255, 255, 0,   255};
        j.Check(OH_PixelmapNative_ReadPixels(
                    static_cast<OH_PixelmapNative*>(data.image->GetNativeObject()), bytes.data(),
                    &size) == IMAGE_SUCCESS &&
                    size == 24 && bytes == expected,
                "concurrent image snapshot contains all independent peer RGBA pixels");
      }
      for (size_t i = 0; i < data.file_paths.size(); ++i) {
        std::ifstream file(data.file_paths[i], std::ios::binary);
        j.Check(std::string(std::istreambuf_iterator<char>(file), {}) ==
                    "peer-payload-" + std::to_string(i) + "-中文🙂",
                "concurrent file snapshot opens the actual foreign producer bytes");
      }
    }
    ++j.callbacks;
    if (++completed == 30) {
      auto keep = shared_from_this();
      ohos::Post([keep] {
        auto& j = clipboard_boundary;
        ++j.batches;
        j.Report(keep->command, keep->request);
      });
    }
  }
  struct Context {
    std::shared_ptr<ClipboardReadBatch> batch;
    int index;
  };
  static void Release(void* data) {
    std::unique_ptr<Context> held(static_cast<Context*>(data));
    ++clipboard_boundary.c_releases;
    clipboard_boundary.Check(IsMainThread(),
                             "C read user data releases on UI after callback/lease completion");
  }
  static void CComplete(bool ok,
                        ClipboardData value,
                        native_event_delivery_t delivery,
                        void* data) {
    auto& context = *static_cast<Context*>(data);
    auto batch = context.batch;
    clipboard_boundary.Check(native_event_delivery_is_active(delivery),
                             "concurrent C read owns an active payload lease");
    batch->Complete(context.index, ok, std::move(value));
    clipboard_boundary.Check(native_event_delivery_complete(delivery, false) &&
                                 !native_event_delivery_complete(delivery, false),
                             "concurrent C read payload lease completes exactly once");
  }
  void Start(int index, bool use_c) {
    const auto format = index % 5;
    auto keep = shared_from_this();
    if (!use_c) {
      auto done = [keep, index](bool ok, ClipboardData data) {
        keep->Complete(index, ok, std::move(data));
      };
      auto& b = Clipboard::GetInstance();
      if (format == 0)
        b.Read(done);
      if (format == 1)
        b.ReadText([done](bool ok, auto value) {
          ClipboardData d;
          d.text = std::move(value);
          done(ok, std::move(d));
        });
      if (format == 2)
        b.ReadHtml([done](bool ok, auto value) {
          ClipboardData d;
          d.html = std::move(value);
          done(ok, std::move(d));
        });
      if (format == 3)
        b.ReadImage([done](bool ok, auto value) {
          ClipboardData d;
          d.image = std::move(value);
          done(ok, std::move(d));
        });
      if (format == 4)
        b.ReadFilePaths([done](bool ok, auto value) {
          ClipboardData d;
          d.file_paths = std::move(value);
          done(ok, std::move(d));
        });
    } else {
      auto* context = new Context{keep, index};
      if (format == 0)
        native_clipboard_read(
            [](bool ok, const native_clipboard_data_t* value, native_event_delivery_t delivery,
               void* data) {
              CComplete(ok, value ? to_cpp_clipboard_data(*value) : ClipboardData{}, delivery,
                        data);
            },
            context, Release);
      if (format == 1)
        native_clipboard_read_text(
            [](bool ok, const char* value, native_event_delivery_t delivery, void* data) {
              ClipboardData d;
              if (value)
                d.text = value;
              CComplete(ok, std::move(d), delivery, data);
            },
            context, Release);
      if (format == 2)
        native_clipboard_read_html(
            [](bool ok, const char* value, native_event_delivery_t delivery, void* data) {
              ClipboardData d;
              if (value)
                d.html = value;
              CComplete(ok, std::move(d), delivery, data);
            },
            context, Release);
      if (format == 3)
        native_clipboard_read_image(
            [](bool ok, native_image_t value, native_event_delivery_t delivery, void* data) {
              native_clipboard_data_t c{};
              c.image = value;
              CComplete(ok, to_cpp_clipboard_data(c), delivery, data);
            },
            context, Release);
      if (format == 4)
        native_clipboard_read_file_paths(
            [](bool ok, const native_string_list_t* value, native_event_delivery_t delivery,
               void* data) {
              native_clipboard_data_t c{};
              if (value)
                c.file_paths = *value;
              CComplete(ok, to_cpp_clipboard_data(c), delivery, data);
            },
            context, Release);
    }
    returned[index].store(true);
  }
};
inline napi_value ClipboardBoundary(napi_env env, napi_callback_info info) {
  size_t count = 2;
  napi_value args[2]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  const auto command = count ? ohos::js::String(env, args[0]) : std::string{};
  const auto request = count > 1 ? ohos::js::String(env, args[1]) : std::string{};
  auto& j = clipboard_boundary;
  auto& b = Clipboard::GetInstance();
  if (command == "boundary-burst" || command == "boundary-pending" ||
      command == "boundary-denied") {
    auto batch = std::make_shared<ClipboardReadBatch>();
    batch->command = command;
    batch->request = request;
    batch->invalidated = command == "boundary-pending";
    batch->denied = command == "boundary-denied";
    if (batch->invalidated)
      j.Check(b.Write(ClipboardInteropPublication()),
              "real mixed data seeded before queued-read replacement");
    for (int i = 0; i < 10; ++i)
      batch->Start(i, i >= 5);
    std::array<std::thread, 4> workers;
    for (int w = 0; w < 4; ++w)
      workers[w] = std::thread([batch, w] {
        for (int k = 0; k < 5; ++k)
          batch->Start(10 + w * 5 + k, w >= 2);
      });
    for (auto& worker : workers)
      worker.join();
    if (batch->invalidated)
      j.Check(b.WriteText("replacement-after-submission"),
              "actual native write changes the revision after every read was submitted");
    return nullptr;
  }
  if (command == "boundary-listen")
    j.Listen();
  if (command == "boundary-stop")
    j.Stop();
  if (command == "boundary-listen-worker") {
    std::thread worker([&] { j.Listen(); });
    worker.join();
  }
  if (command == "boundary-stop-worker") {
    std::thread worker([&] { j.Stop(); });
    worker.join();
  }
  if (command == "boundary-reference") {
    j.board = OH_Pasteboard_Create();
    j.reference = OH_PasteboardObserver_Create();
    j.Check(j.board && j.reference &&
                OH_PasteboardObserver_SetData(
                    j.reference, &j,
                    [](void* data, Pasteboard_NotifyType) {
                      ++static_cast<ClipboardBoundaryJournal*>(data)->references;
                    },
                    [](void*) {}) == ERR_OK &&
                OH_Pasteboard_Subscribe(j.board, NOTIFY_LOCAL_DATA_CHANGE, j.reference) == ERR_OK,
            "independent direct SDK change reference subscribed");
  }
  if (command == "boundary-sdk-denied") {
    OH_Pasteboard* probe = OH_Pasteboard_Create();
    int status = ERR_INNER_ERROR;
    OH_UdmfData* data = OH_Pasteboard_GetData(probe, &status);
    j.Check(status == ERR_PERMISSION_ERROR && !data,
            "independent public NDK read reports actual permission refusal");
    if (data)
      OH_UdmfData_Destroy(data);
    OH_Pasteboard_Destroy(probe);
  }
  if (command == "boundary-publish-directory" || command == "boundary-publish-directory-c") {
    auto data = ClipboardInteropPublication();
    const auto directory =
        std::filesystem::path(data.file_paths.front()).parent_path() / "folder-中文 +#%";
    std::filesystem::create_directories(directory / "nested");
    std::ofstream(directory / "payload.txt") << "native-folder-中文🙂";
    std::ofstream(directory / "nested/child.txt") << "native-nested-中文🙂";
    data = {};
    data.file_paths = {directory.string()};
    bool ok;
    if (command == "boundary-publish-directory-c") {
      auto value = to_c_clipboard_data(data);
      ok = native_clipboard_write(value);
      native_clipboard_data_free(&value);
    } else
      ok = b.Write(data);
    j.Check(ok, "C++/C directory publication uses an existing native folder URI");
  }
  if (command == "boundary-remember-files") {
    b.ReadFilePaths([command, request](bool ok, auto paths) {
      auto& j = clipboard_boundary;
      j.Check(ok && paths.size() == 2, "remember two actually authorized producer files");
      for (const auto& path : paths)
        j.Check(access(path.c_str(), R_OK) == 0,
                "remembered foreign file is readable by the real app UID");
      j.foreign_files = std::move(paths);
      j.Report(command, request);
    });
    return nullptr;
  }
  if (command == "boundary-files-unreadable") {
    j.Check(j.foreign_files.size() == 2, "revocation check retains two original foreign paths");
    for (const auto& path : j.foreign_files) {
      std::ifstream file(path, std::ios::binary);
      j.Check(!file, "cached foreign path cannot bypass actual revoked/removed producer data");
    }
  }
  if (command == "boundary-hold")
    display_test_dispatcher.Hold();
  if (command == "boundary-reject-dispatch")
    SetMainThreadDispatcher([](auto) { return false; }, nullptr);
  if (command == "boundary-restore-dispatch")
    InstallDisplayTestDispatcher();
  if (command == "boundary-replace") {
    j.Check(j.Held() > 0, "real SDK notice work is pending before observer epoch replacement");
    const auto before = std::array<int, 2>{j.cpp, j.c};
    j.Stop();
    j.Listen();
    display_test_dispatcher.Drain();
    j.Check(before == std::array<int, 2>{j.cpp, j.c},
            "old native clipboard notices cannot reach new observer epochs");
  }
  if (command == "boundary-drain")
    display_test_dispatcher.Drain();
  if (command == "boundary-self-remove")
    j.self_remove = true;
  if (command == "boundary-partial") {
    j.Check(native_clipboard_remove_listener(j.c_listener),
            "partial C observer removal leaves C++ subscription active");
    j.c_listener = 0;
    j.Check(b.IsMonitoring(), "partial removal preserves the actual SDK subscription");
  }
  if (command == "boundary-finish") {
    InstallDisplayTestDispatcher();
    j.self_remove = false;
    j.Stop();
    display_test_dispatcher.Drain();
    if (j.reference) {
      OH_Pasteboard_Unsubscribe(j.board, NOTIFY_LOCAL_DATA_CHANGE, j.reference);
      OH_PasteboardObserver_Destroy(j.reference);
      OH_Pasteboard_Destroy(j.board);
      j.reference = nullptr;
      j.board = nullptr;
    }
  }
  j.Report(command, request);
  return nullptr;
}
}  // namespace nativeapi::tests
