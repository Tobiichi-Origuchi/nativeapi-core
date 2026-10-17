// Validate the real SNI pixmaps and appearance notifications without a desktop.
#include "nativeapi.h"
#include <gio/gio.h>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

namespace {
void Require(bool ok, const char* name) {
  std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
  if (!ok) std::exit(1);
}

bool Wait(const std::function<bool()>& done) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!done() && std::chrono::steady_clock::now() < deadline) {
    while (g_main_context_iteration(nullptr, FALSE)) {}
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return done();
}

std::vector<guint8> Pixels(GDBusConnection* client, const std::string& service) {
  struct Result { bool done = false; GVariant* reply = nullptr; } result;
  g_dbus_connection_call(client, service.c_str(), "/StatusNotifierItem",
      "org.freedesktop.DBus.Properties", "Get",
      g_variant_new("(ss)", "org.kde.StatusNotifierItem", "IconPixmap"),
      G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr,
      [](GObject* source, GAsyncResult* async, gpointer data) {
        auto* result = static_cast<Result*>(data);
        result->reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), async, nullptr);
        result->done = true;
      }, &result);
  Require(Wait([&] { return result.done; }) && result.reply, "read SNI IconPixmap");
  GVariant* array = nullptr;
  g_variant_get(result.reply, "(v)", &array);
  Require(g_variant_n_children(array) == 1, "one pixmap exported");
  GVariant* entry = g_variant_get_child_value(array, 0);
  gint width, height;
  GVariant* bytes = nullptr;
  g_variant_get(entry, "(ii@ay)", &width, &height, &bytes);
  Require(width == 3 && height == 1, "original dimensions preserved");
  gsize length;
  const auto* raw = static_cast<const guint8*>(g_variant_get_fixed_array(bytes, &length, 1));
  std::vector<guint8> pixels(raw, raw + length);
  g_variant_unref(bytes);
  g_variant_unref(entry);
  g_variant_unref(array);
  g_variant_unref(result.reply);
  return pixels;
}

void Scheme(GDBusConnection* client, guint32 value) {
  Require(g_dbus_connection_emit_signal(client, nullptr, "/org/freedesktop/portal/desktop",
      "org.freedesktop.portal.Settings", "SettingChanged",
      g_variant_new("(ssv)", "org.freedesktop.appearance", "color-scheme",
                    g_variant_new_uint32(value)), nullptr), "emit portal scheme change");
  g_dbus_connection_flush_sync(client, nullptr, nullptr);
}
}

int main() {
  GTestDBus* bus = g_test_dbus_new(G_TEST_DBUS_NONE);
  g_test_dbus_up(bus);
  gchar* directory = g_dir_make_tmp("nativeapi-tray-theme-XXXXXX", nullptr);
  Require(directory != nullptr, "temporary palette directory");
  const auto theme = std::filesystem::path(directory) / "theme";
  std::filesystem::create_directories(theme);
  const auto palette = (theme / "colors.toml").string();
  g_setenv("OMARCHY_STATE_DIR", directory, TRUE);
  g_file_set_contents(palette.c_str(), "foreground = '#123456'\n", -1, nullptr);
  auto* client = g_dbus_connection_new_for_address_sync(g_getenv("DBUS_SESSION_BUS_ADDRESS"),
      static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                       G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
      nullptr, nullptr, nullptr);
  Require(client != nullptr, "private test bus connected");
  auto* name = g_dbus_connection_call_sync(client, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "RequestName",
      g_variant_new("(su)", "org.freedesktop.portal.Desktop", 0u), G_VARIANT_TYPE("(u)"),
      G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
  Require(name != nullptr, "own fake portal name");
  g_variant_unref(name);
  auto image = nativeapi::Image::FromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAMAAAABCAYAAAAb4BS0AAAAE0lEQVR4nGO4I2fzn0tEroEBCAAbBgLyKTwmhAAAAABJRU5ErkJggg==");
  Require(image != nullptr, "load alpha-mask fixture");
  const auto source = image->ToBase64();
  const std::string service = "org.kde.StatusNotifierItem-" + std::to_string(getpid()) + "-1";
  {
    nativeapi::TrayIcon icon;
    icon.SetIcon(image);
    Require(Wait([&] {
      return g_dbus_connection_get_unique_name(client) != nullptr;
    }), "bus ready");
    const auto original = Pixels(client, service);
    Require(original[1] == 220 && original[2] == 30 && original[3] == 60, "original colours exported");
    icon.SetIconTemplate(true);
    auto pixels = Pixels(client, service);
    Require(pixels[1] == 0x12 && pixels[2] == 0x34 && pixels[3] == 0x56, "Omarchy foreground applied");
    Require(pixels[0] == 255 && pixels[4] == 128 && pixels[8] == 0, "alpha mask preserved");
    g_file_set_contents(palette.c_str(), "foreground = '#abcdef'\n", -1, nullptr);
    Require(Wait([&] { return Pixels(client, service)[1] == 0xab; }), "live palette change updates pixmap");
    std::filesystem::remove(palette);
    Scheme(client, 1);
    Require(Wait([&] { return Pixels(client, service)[1] == 255; }), "portal dark mode uses white");
    Scheme(client, 2);
    Require(Wait([&] { return Pixels(client, service)[1] == 0; }), "portal light mode uses black");
    icon.SetIconTemplate(false);
    Require(Pixels(client, service) == original, "disabling template restores original pixels");
    Scheme(client, 1);
    Require(Pixels(client, service) == original, "fixed colours ignore appearance changes");
    Require(image->ToBase64() == source && icon.GetIcon() == image, "source image stays shared and unchanged");
    icon.SetIconTemplate(true);
  }
  // Dispatch after destruction: subscriptions and the palette timer must be gone.
  Scheme(client, 2);
  while (g_main_context_iteration(nullptr, FALSE)) {}
  g_dbus_connection_close_sync(client, nullptr, nullptr);
  g_object_unref(client);
  g_unsetenv("OMARCHY_STATE_DIR");
  std::filesystem::remove_all(directory);
  g_free(directory);
  g_test_dbus_down(bus);
  g_object_unref(bus);
  return 0;
}
