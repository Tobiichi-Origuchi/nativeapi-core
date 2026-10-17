#pragma once
// Real SDK-backed integration assertions; only the disposable HAP sandbox is modified.
#include <multimedia/image_framework/image/pixelmap_native.h>
#include <sys/stat.h>
#include <unistd.h>
#include <array>
#include <cstdio>
#include <functional>
#include "../../../../../../src/app_info.h"
#include "../../../../../../src/capi/app_info_c.h"
#include "../../../../../../src/capi/image_c.h"
#include "../../../../../../src/capi/secure_storage_c.h"
#include "../../../../../../src/capi/string_utils_c.h"
#include "../../../../../../src/image.h"
#include "../../../../../../src/secure_storage.h"

namespace nativeapi::tests {
inline constexpr const char* FixturePng =
    "iVBORw0KGgoAAAANSUhEUgAAAAMAAAACCAYAAACddGYaAAAAGElEQVR4nGP4z8DwHwwZ/"
    "gNJIGAAsYBMAKhxDfO3Sm0qAAAAAElFTkSuQmCC";
inline bool FixturePixels(const std::shared_ptr<Image>& image) {
  const std::array<uint8_t, 24> expected{255, 0,   0,   255, 0, 255, 0, 255, 0,   0,   255, 255,
                                         255, 255, 255, 255, 0, 0,   0, 255, 255, 255, 0,   255};
  if (!image || image->GetSize().width != 3 || image->GetSize().height != 2)
    return false;
  std::array<uint8_t, 24> actual{};
  size_t size = actual.size();
  return OH_PixelmapNative_ReadPixels(static_cast<OH_PixelmapNative*>(image->GetNativeObject()),
                                      actual.data(), &size) == IMAGE_SUCCESS &&
         actual == expected;
}
inline std::shared_ptr<Image> FixtureImage() {
  return Image::FromBase64(FixturePng);
}
inline void Services(const std::function<void(bool, const char*)>& check) {
  auto& app = AppInfo::GetInstance();
  check(app.GetIdentifier() == "dev.nativeapi.smoke" && app.GetVersion() == "1.0.0" &&
            app.GetBuildNumber() == "1",
        "application metadata matches the installed bundle manifest");
  check(app.GetName() == "NativeAPI Smoke",
        "application name resolves the actual packaged label resource");
  char* identifier = native_app_info_get_identifier();
  check(identifier && app.GetIdentifier() == identifier,
        "generated C ABI exposes installed bundle metadata");
  free_c_str(identifier);
  const char* png = FixturePng;
  auto pixels_match = FixturePixels;
  auto image = Image::FromBase64(png);
  check(image && image->GetFormat() == "PNG" && pixels_match(image),
        "image PNG decoding yields actual SDK RGBA pixels and detected format");
  check(!Image::FromBase64("@@@=") && !Image::FromBase64("Zg=a") && !Image::FromBase64("Zh==") &&
            !Image::FromBase64("data:image/png,abc") && !Image::FromBase64("bm90IGFuIGltYWdl"),
        "image malformed base64 and invalid encoded images are rejected");
  check(!Image::FromFile("/data/storage/el2/base/files/nativeapi-nonexistent.png"),
        "image missing files fail without constructing an invalid image");
  if (image) {
    const auto encoded = image->ToBase64();
    check(
        encoded.rfind("data:image/png;base64,", 0) == 0 && pixels_match(Image::FromBase64(encoded)),
        "image PNG packing and data URI decoding preserve every pixel");
    auto copy = std::make_shared<Image>(*image);
    Image moved(std::move(*copy));
    image.reset();
    check(moved.GetNativeObject() && !copy->GetNativeObject() && copy->GetSize().width == 0,
          "image copies retain native pixels and moved-from access stays safe");
    mkdir("/data/storage/el2/base/files", 0700);
    const std::string prefix = "/data/storage/el2/base/files/nativeapi-image-smoke";
    check(moved.SaveToFile(prefix + ".png") && pixels_match(Image::FromFile(prefix + ".png")),
          "image PNG file round trip preserves actual pixels in the app sandbox");
    rename((prefix + ".png").c_str(), (prefix + ".bin").c_str());
    auto renamed = Image::FromFile(prefix + ".bin");
    check(renamed && renamed->GetFormat() == "PNG",
          "image format is detected from content rather than filename");
    check(moved.SaveToFile(prefix + ".jpg") && Image::FromFile(prefix + ".jpg") &&
              Image::FromFile(prefix + ".jpg")->GetFormat() == "JPEG",
          "image native JPEG encoding and decoding");
    check(moved.SaveToFile(prefix + ".webp") && Image::FromFile(prefix + ".webp") &&
              Image::FromFile(prefix + ".webp")->GetFormat() == "WEBP",
          "image native WebP encoding and decoding");
    check(!moved.SaveToFile(prefix + ".unsupported") && !moved.SaveToFile("/no-such-dir/image.png"),
          "image unsupported output and inaccessible paths report failure");
    for (const char* extension : {".png", ".bin", ".jpg", ".webp"})
      unlink((prefix + extension).c_str());
  }
  auto handle = native_image_from_base64(png);
  const auto size = native_image_get_size(handle);
  check(handle && size.width == 3 && size.height == 2, "generated C ABI exposes decoded images");
  native_image_free(handle);

  check(SecureStorage::IsAvailable(), "system ASSET encrypted storage service is available");
  SecureStorage storage("ohos-smoke-secure/隔离"), isolated("ohos-smoke-secure-isolated");
  check(storage.Clear() && isolated.Clear(), "secure storage scoped cleanup");
  check(storage.GetScope() == "ohos-smoke-secure/隔离" && SecureStorage().GetScope() == "default",
        "secure storage explicit and default scopes");
  check(storage.Set("unicode", "秘密🙂") && storage.Set("empty", "") &&
            storage.Set("", "empty key") && storage.Set("sentinel", "nativeapi-invalid-secret"),
        "secure storage Unicode and empty strings persist through ASSET");
  check(storage.Get("unicode") == "秘密🙂" && storage.Get("empty", "fallback").empty() &&
            storage.Get("") == "empty key",
        "secure storage reads preserve Unicode and empty values");
  check(storage.Get("absent", "fallback") == "fallback" && !storage.Contains("absent"),
        "secure storage missing value defaults and membership");
  check(storage.GetKeys().size() == 4 && storage.GetSize() == 4 && storage.GetAll().size() == 4 &&
            storage.GetAll()["sentinel"] == "nativeapi-invalid-secret",
        "secure storage enumeration does not confuse values with failure sentinels");
  const std::string binary("new\0secret", 10);
  check(storage.Set("unicode", binary) && storage.Get("unicode") == binary,
        "secure storage atomically replaces secrets and preserves binary string values");
  check(isolated.Set("unicode", "other") && storage.Get("unicode") == binary &&
            isolated.GetSize() == 1,
        "secure storage scopes isolate identical keys");
  check(storage.Set("limit", std::string(1023, 'x')) &&
            !storage.Set("limit", std::string(1024, 'y')) &&
            storage.Get("limit") == std::string(1023, 'x'),
        "secure storage enforces native secret limits before replacing existing data");
  check(!storage.Set(std::string(300, 'k'), "invalid") &&
            !storage.Set(std::string("nul\0key", 7), "invalid"),
        "secure storage rejects unsupported aliases without writing partial assets");
  bool enumerated = true;
  for (int i = 0; i < 40; ++i)
    enumerated &= storage.Set("enumeration-" + std::to_string(i), "test");
  check(enumerated && storage.GetSize() == 45,
        "secure storage enumerates more than one small result page");
  {
    SecureStorage reopened("ohos-smoke-secure/隔离");
    check(reopened.Get("unicode") == binary && reopened.Contains("empty"),
          "secure storage survives instance close and reopen");
  }
  check(storage.Remove("unicode") && !storage.Remove("unicode") && !storage.Contains("unicode"),
        "secure storage deletion reports missing keys correctly");
  check(storage.Clear() && storage.GetAll().empty() && isolated.Get("unicode") == "other" &&
            isolated.Clear(),
        "secure storage clear is restricted to its own namespace");
  SecureStorage persistent("ohos-smoke-secure-process-persistence");
  const auto prior = persistent.Get("marker");
  check(prior.empty() ? persistent.Set("marker", "persisted secret 中文🙂")
                      : prior == "persisted secret 中文🙂",
        prior.empty() ? "seed ASSET cross-process persistence"
                      : "ASSET secrets survive process restart");
  const auto secure = native_secure_storage_create_with_scope("ohos-smoke-secure-capi");
  const bool written = native_secure_storage_set(secure, "key", "C ABI secret");
  char* value = native_secure_storage_get(secure, "key", "fallback");
  check(secure && written && value && std::string(value) == "C ABI secret",
        "generated C ABI reads and writes system encrypted secrets");
  free_c_str(value);
  native_secure_storage_clear(secure);
  native_secure_storage_free(secure);
}
}  // namespace nativeapi::tests
