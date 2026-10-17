#import <Cocoa/Cocoa.h>
#include "nativeapi.h"
#include <cstdlib>
#include <iostream>

static void Require(bool ok, const char* name) {
  std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
  if (!ok) std::exit(1);
}

int main() {
  @autoreleasepool {
    [NSApplication sharedApplication];
    auto image = nativeapi::Image::FromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAMAAAABCAYAAAAb4BS0AAAAE0lEQVR4nGO4I2fzn0tEroEBCAAbBgLyKTwmhAAAAABJRU5ErkJggg==");
    Require(image != nullptr, "load alpha-mask fixture");
    NSImage* source = (__bridge NSImage*)image->GetNativeObject();
    const std::string original = image->ToBase64();
    nativeapi::TrayIcon tray;
    NSStatusItem* item = (__bridge NSStatusItem*)tray.GetNativeObject();
    Require(item.button != nil, "native status item created");
    tray.SetIcon(image);
    tray.SetIconTemplate(true);
    Require(item.button.image.isTemplate, "existing image becomes native template");
    for (NSAppearanceName name in @[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]) {
      item.button.appearance = [NSAppearance appearanceNamed:name];
      [item.button displayIfNeeded];
      Require(item.button.image.isTemplate, "system appearance keeps native template tinting");
    }
    Require(!source.isTemplate && image->ToBase64() == original, "shared source stays unchanged");
    tray.SetIconTemplate(false);
    Require(!item.button.image.isTemplate, "disabling template restores colour mode");
    tray.SetIconTemplate(true);
    tray.SetIcon(image);
    Require(item.button.image.isTemplate, "new animation frames retain template mode");
    tray.SetIcon(nullptr);
    Require(item.button.image == nil && tray.IsIconTemplate(), "clear image retains template preference");
  }
  return 0;
}
