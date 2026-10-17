#include <windows.h>
#include <gdiplus.h>
#include "nativeapi.h"
#include <cstdlib>
#include <iostream>
#include <vector>

namespace nativeapi {
HICON ImageToTemplateHICON(const Image*, int, int, COLORREF);
}

static void Require(bool ok, const char* name) {
  std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
  if (!ok) std::exit(1);
}

int main() {
  auto image = nativeapi::Image::FromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAMAAAABCAYAAAAb4BS0AAAAE0lEQVR4nGO4I2fzn0tEroEBCAAbBgLyKTwmhAAAAABJRU5ErkJggg==");
  Require(image != nullptr, "load alpha-mask fixture");
  const std::string original = image->ToBase64();
  for (const COLORREF tint : {RGB(0, 0, 0), RGB(255, 255, 255), RGB(40, 120, 200)}) {
    HICON icon = nativeapi::ImageToTemplateHICON(image.get(), 3, 1, tint);
    Require(icon != nullptr, "render template HICON");
    ICONINFO info{};
    Require(GetIconInfo(icon, &info) != FALSE, "read HICON bitmap");
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = 3;
    bitmap.bmiHeader.biHeight = -1;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;
    std::vector<unsigned char> pixels(12);
    HDC dc = GetDC(nullptr);
    Require(GetDIBits(dc, info.hbmColor, 0, 1, pixels.data(), &bitmap, DIB_RGB_COLORS) == 1,
            "read rendered pixels");
    ReleaseDC(nullptr, dc);
    Require(pixels[3] == 255 && pixels[7] >= 127 && pixels[7] <= 129 && pixels[11] == 0,
            "opaque, partial and transparent alpha survive");
    Require(pixels[0] == GetBValue(tint) && pixels[1] == GetGValue(tint) &&
                pixels[2] == GetRValue(tint), "foreground RGB applied");
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    DestroyIcon(icon);
    Require(image->ToBase64() == original, "shared source image remains unchanged");
  }
  nativeapi::TrayIcon tray;
  tray.SetIcon(image);
  tray.SetIconTemplate(true);
  Require(tray.IsIconTemplate() && tray.GetIcon() == image, "template enabled on existing image");
  // Broadcast receipt is independent of tray event listeners. Only our own
  // hidden watcher gets this synthetic notification; no system settings change.
  HWND watcher = FindWindowW(L"NativeAPITaskbarRestartWatcher", nullptr);
  Require(watcher != nullptr, "appearance watcher exists without event listeners");
  SendMessageW(watcher, WM_SETTINGCHANGE, 0, 0);
  tray.SetIconTemplate(false);
  Require(!tray.IsIconTemplate() && image->ToBase64() == original, "template disabled preserves source");
  return 0;
}
