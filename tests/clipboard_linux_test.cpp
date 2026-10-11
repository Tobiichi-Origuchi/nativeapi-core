// Explicit integration test: run in a private Xvfb session, never on a user's clipboard.
#include <gtk/gtk.h>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include "../src/clipboard.h"
#include "../src/foundation/dispatcher.h"
#include "../src/image.h"

using namespace nativeapi;
namespace {
void Check(bool value, const char* label) {
  std::cout << (value ? "PASS " : "FAIL ") << label << std::endl;
  if (!value)
    throw std::runtime_error(label);
}
void Pump(int milliseconds) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
  while (std::chrono::steady_clock::now() < end)
    RunMainThreadLoopFor(5);
}
ClipboardData Read() {
  ClipboardData result;
  bool done = false, success = false;
  Clipboard::GetInstance().Read([&](bool ok, ClipboardData data) {
    success = ok;
    result = std::move(data);
    done = true;
  });
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(6);
  while (!done && std::chrono::steady_clock::now() < end)
    RunMainThreadLoopFor(5);
  Check(done && success, "asynchronous snapshot");
  return result;
}
}  // namespace
int main() {
  if (!gtk_init_check(nullptr, nullptr))
    return 77;
  try {
    auto& board = Clipboard::GetInstance();
    Check(board.IsSupported() && board.IsChangeMonitoringSupported(), "capabilities");
    int events = 0;
    auto listener = board.AddListener<ClipboardChangedEvent>([&](const auto&) { ++events; });
    Pump(200);
    Check(board.IsMonitoring() && events == 0, "no initial notification");
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY), "selection only", -1);
    Pump(200);
    Check(events == 0, "PRIMARY is independent of copy/paste");
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), "external 中文🙂", -1);
    Pump(250);
    Check(events > 0, "GTK copy/paste notification");
    Check(Read().text == "external 中文🙂", "external unicode read");
    ClipboardData data;
    data.text = "fallback";
    data.html = "<b>中文🙂</b>";
    data.image = Image::FromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAAH/"
        "iZk9HQAAAABJRU5ErkJggg==");
    data.file_paths = {"/tmp/nativeapi-a.txt", "/tmp/nativeapi-中文.txt"};
    Check(data.image && board.Write(data), "multi-format write");
    Pump(200);
    auto content = Read();
    Check(content.text == data.text && content.html == data.html, "text HTML snapshot");
    Check(content.image && content.image->GetSize().width == 1, "image snapshot");
    Check(content.file_paths == data.file_paths, "file paths snapshot");
    Check(board.WriteText(""), "empty text write");
    Pump(200);
    content = Read();
    Check(content.text && content.text->empty() && !content.html && !content.image &&
              content.file_paths.empty(),
          "empty differs from absent and replaces formats");
    Check(board.Clear(), "clear");
    Pump(200);
    Check(!Read().text, "absent text");
    board.RemoveListener(listener);
    Check(!board.IsMonitoring(), "last listener stops");
    const int previous = events;
    Check(board.WriteText("after stop"), "write after removal");
    Pump(200);
    Check(events == previous, "no events after removal");
    listener = board.AddListener<ClipboardChangedEvent>([&](const auto&) { ++events; });
    Check(board.WriteText("after resubscribe"), "write after resubscribe");
    Pump(250);
    Check(events > previous, "resubscribe notification");
    board.RemoveListener(listener);
    std::cout << "NATIVEAPI_LINUX_CLIPBOARD_PASS" << std::endl;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
