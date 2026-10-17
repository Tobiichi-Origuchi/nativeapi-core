#pragma once
#include <iomanip>
#include <sys/stat.h>
#include <unistd.h>
#include "../../../../../../src/capi/clipboard_c.h"
#include "../../../../../../src/platform/ohos/ohos_file_uri.h"

namespace nativeapi::tests {
inline std::string ClipboardJson(const std::string& text) {
  std::ostringstream out;
  out << '"';
  for (unsigned char c : text) {
    if (c == '"' || c == '\\') out << '\\' << c;
    else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
    else out << c;
  }
  out << '"';
  return out.str();
}
struct ClipboardInteropJournal {
  int cpp = 0, c = 0, failed = 0, passed = 0;
  std::vector<std::string> seeded;
  size_t listener = 0;
  native_listener_id_t c_listener = 0;
  void Check(bool ok, const char* text) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-clipboard",
                 "%{public}s %{public}s", ok ? "PASS" : "FAIL", text);
  }
  void Report(const std::string& command, const std::string& request, bool ok,
              const ClipboardData& data = {}) {
    std::array<uint8_t, 24> pixels{};
    size_t pixel_count = data.image ? pixels.size() : 0;
    if (data.image) {
      const bool read = OH_PixelmapNative_ReadPixels(
          static_cast<OH_PixelmapNative*>(data.image->GetNativeObject()), pixels.data(), &pixel_count) == IMAGE_SUCCESS;
      Check(read && pixel_count == pixels.size(), "received independent image exposes its real RGBA bytes");
      if (!read || pixel_count != pixels.size()) pixel_count = 0;
    }
    std::ostringstream out;
    out << "{\"ok\":" << (ok ? "true" : "false") << ",\"passed\":" << passed
        << ",\"failed\":" << failed << ",\"cpp\":" << cpp << ",\"c\":" << c
        << ",\"monitoring\":" << (Clipboard::GetInstance().IsMonitoring() ? "true" : "false")
        << ",\"text\":" << (data.text ? ClipboardJson(*data.text) : "null")
        << ",\"html\":" << (data.html ? ClipboardJson(*data.html) : "null") << ",\"paths\":[";
    for (size_t i = 0; i < data.file_paths.size(); ++i) {
      if (i) out << ',';
      out << ClipboardJson(data.file_paths[i]);
    }
    out << "],\"contents\":[";
    for (size_t i = 0; i < data.file_paths.size(); ++i) {
      if (i) out << ',';
      const auto& path = data.file_paths[i];
      if (std::filesystem::is_directory(path)) {
        std::ifstream first(path + "/payload.txt", std::ios::binary);
        std::ifstream second(path + "/nested/child.txt", std::ios::binary);
        out << ClipboardJson("folder:" + std::string(std::istreambuf_iterator<char>(first), {}) +
                             "|" + std::string(std::istreambuf_iterator<char>(second), {}));
      } else {
        std::ifstream file(path, std::ios::binary);
        out << ClipboardJson(std::string(std::istreambuf_iterator<char>(file), {}));
      }
    }
    out << "],\"pixels\":[";
    for (size_t i = 0; i < pixel_count; ++i) {
      if (i) out << ',';
      out << int(pixels[i]);
    }
    out << "],\"seededPaths\":[";
    for (size_t i = 0; i < seeded.size(); ++i) {
      if (i) out << ',';
      out << ClipboardJson(seeded[i]);
    }
    out << "]}";
    OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-clipboard",
                 "NATIVEAPI_CLIPBOARD_REPLY %{public}s %{public}s %{public}s",
                 request.c_str(), command.c_str(), out.str().c_str());
  }
};
inline ClipboardInteropJournal clipboard_interop;
inline ClipboardData ClipboardInteropPublication() {
  ClipboardData data;
  data.text = "native-text-中文🙂";
  data.html = "<b>native-html-中文🙂</b>";
  data.image = FixtureImage();
  std::string root;
  ohos::WithContext([&](napi_env env, napi_value context) {
    root = ohos::js::StringProperty(env, context, "filesDir") + "/";
  });
  mkdir(root.c_str(), 0700);
  data.file_paths = {root + "cross-app-中文 space+#%.txt", root + "second.txt"};
  for (size_t i = 0; i < data.file_paths.size(); ++i)
    std::ofstream(data.file_paths[i]) << "native-payload-" << i << "-中文🙂";
  clipboard_interop.seeded = data.file_paths;
  return data;
}
inline napi_value ClipboardInterop(napi_env env, napi_callback_info info) {
  napi_value args[2]{};
  size_t count = 2;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  const auto command = count ? ohos::js::String(env, args[0]) : std::string();
  const auto request = count > 1 ? ohos::js::String(env, args[1]) : std::string();
  auto& j = clipboard_interop;
  auto& board = Clipboard::GetInstance();
  if (command == "listen") {
    if (!j.listener) j.listener = board.AddListener<ClipboardChangedEvent>([](const auto&) {
      ++clipboard_interop.cpp;
      clipboard_interop.Check(IsMainThread(), "external clipboard C++ event arrives on UI");
    });
    if (!j.c_listener) j.c_listener = native_clipboard_add_listener([](const auto*, void*) {
      ++clipboard_interop.c;
      clipboard_interop.Check(IsMainThread(), "external clipboard C event arrives on UI");
    }, nullptr, nullptr);
    j.Check(j.listener && j.c_listener && board.IsMonitoring(), "C++ and C clipboard observers installed");
  }
  if (command == "unlisten") {
    const bool active = j.listener || j.c_listener;
    if (j.listener) j.Check(board.RemoveListener(j.listener), "C++ clipboard observer removed");
    if (j.c_listener) j.Check(native_clipboard_remove_listener(j.c_listener), "C clipboard observer removed");
    j.listener = j.c_listener = 0;
    if (active) j.Check(!board.IsMonitoring(), "last clipboard observer stops native monitoring");
  }
  if (command == "publish" || command == "publish-c") {
    auto data = ClipboardInteropPublication();
    bool ok;
    if (command == "publish-c") {
      auto c_data = to_c_clipboard_data(data);
      ok = native_clipboard_write(c_data);
      native_clipboard_data_free(&c_data);
    } else ok = board.Write(data);
    j.Check(ok, "real readable multi-format clipboard publication succeeds");
  }
  if (command == "validate-paths") {
    auto data = ClipboardInteropPublication();
    const auto path = data.file_paths[0];
    std::string uri, decoded;
    bool directory = false;
    j.Check(ohos::LocalPathToUri(path, uri, directory) && !directory &&
                ohos::UriToLocalPath(uri, decoded) && decoded == path,
            "public FileUri SDK maps an existing readable own file");
    const auto authority_end = uri.find('/', 7);
    const auto foreign = std::string("file://dev.nativeapi.clipboard.peer") + uri.substr(authority_end);
    auto different_file = [&](const std::string& value) {
      if (!ohos::UriToLocalPath(value, decoded)) return true;
      struct stat own{}, foreign{};
      return stat(path.c_str(), &own) == 0 && stat(decoded.c_str(), &foreign) == 0 &&
             (own.st_dev != foreign.st_dev || own.st_ino != foreign.st_ino);
    };
    j.Check(different_file(foreign), "foreign private URI never aliases the same underlying consumer file");
    auto dotted = foreign;
    dotted.replace(dotted.find("/data/storage/"), 14, "/data/./storage/");
    j.Check(different_file(dotted), "dot segments cannot bypass foreign sandbox identity validation");
    const auto missing = path + ".deleted";
    unlink(missing.c_str());
    { std::ofstream file(missing); file << "temporary"; }
    std::string deleted_uri;
    j.Check(ohos::LocalPathToUri(missing, deleted_uri, directory), "temporary file URI created by real SDK");
    unlink(missing.c_str());
    j.Check(!ohos::UriToLocalPath(deleted_uri, decoded), "deleted files are never exposed as usable clipboard paths");
    const auto denied = path + ".denied";
    { std::ofstream file(denied); file << "denied"; }
    std::string denied_uri;
    j.Check(ohos::LocalPathToUri(denied, denied_uri, directory), "readable file URI seeded before access removal");
    chmod(denied.c_str(), 0000);
    j.Check(access(denied.c_str(), R_OK) != 0 && !ohos::UriToLocalPath(denied_uri, decoded),
            "actual app-UID read denial rejects a clipboard path");
    chmod(denied.c_str(), 0600);
    unlink(denied.c_str());
    j.Check(!ohos::UriToLocalPath("https://example.com/a", decoded), "remote URLs do not become file paths");
  }
  if (command == "read" || command == "read-c" || command == "read-worker" || command == "files") {
    auto returned = std::make_shared<std::atomic<bool>>(false);
    auto callback = [command, request, returned](bool ok, ClipboardData data) {
      auto& j = clipboard_interop;
      j.Check(returned->load() && IsMainThread(), "clipboard completion is deferred to UI even for worker/C reads");
      j.Report(command, request, ok, data);
    };
    if (command == "read-c") {
      using Callback = decltype(callback);
      native_clipboard_read([](bool ok, const native_clipboard_data_t* data,
                               native_event_delivery_t delivery, void* context) {
        auto owned = data ? to_cpp_clipboard_data(*data) : ClipboardData{};
        clipboard_interop.Check(native_event_delivery_is_active(delivery), "C read delivery lease is active");
        (*static_cast<Callback*>(context))(ok, std::move(owned));
        clipboard_interop.Check(native_event_delivery_complete(delivery, false), "C read delivery lease completes once");
      }, new Callback(callback), [](void* context) { delete static_cast<Callback*>(context); });
    } else if (command == "read-worker") {
      std::thread worker([callback] { Clipboard::GetInstance().Read(callback); });
      worker.join();
    } else if (command == "files") {
      board.ReadFilePaths([callback](bool ok, auto files) { ClipboardData data; data.file_paths = std::move(files); callback(ok, std::move(data)); });
    } else board.Read(callback);
    returned->store(true);
  } else j.Report(command, request, true);
  napi_value result;
  napi_get_undefined(env, &result);
  return result;
}
}  // namespace nativeapi::tests
