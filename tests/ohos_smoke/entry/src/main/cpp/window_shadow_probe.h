#pragma once
// SDK-surface feasibility probe: the same core renderer used by other desktops.
#include <multimedia/image_framework/image/pixelmap_native.h>
#include "../../../../../../src/platform/ohos/ohos_napi.h"
#include "../../../../../../src/platform/window_shadow_blur.h"
namespace nativeapi::tests {
inline napi_value ShadowProbeImage(napi_env env, napi_callback_info info) {
  size_t count = 6;
  napi_value args[6]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  double values[6]{};
  if (count != 6)
    return nullptr;
  for (int i = 0; i < 6; ++i)
    if (napi_get_value_double(env, args[i], &values[i]) != napi_ok || !std::isfinite(values[i]))
      return nullptr;
  const int width = static_cast<int>(values[0]), height = static_cast<int>(values[1]);
  const double scale = values[2];
  if (width < 1 || height < 1 || width > 2048 || height > 2048 || scale <= 0 || scale > 8)
    return nullptr;
  WindowShadow options;
  options.SetBlurRadius(values[3]);
  options.SetOffset({values[4], values[5]});
  options.SetColor({255, 0, 0, 192});
  WindowShape polygon;
  auto pixels = window_shadow::Render(width, height, polygon, options, scale);
  const int margin = window_shadow::Margin(options, scale), wide = width + 2 * margin,
            high = height + 2 * margin;
  if (pixels.empty())
    return nullptr;
  // Helpers can be above a main window. Their interior must never dim its content.
  for (int y = 0; y < height; ++y)
    std::fill(pixels.begin() + size_t(y + margin) * wide + margin,
              pixels.begin() + size_t(y + margin) * wide + margin + width, 0);
  // The PixelMap creation path consumes straight source channels. Supplying
  // the renderer's premultiplied RGB here would multiply the color twice.
  for (auto& pixel : pixels)
    if (pixel >> 24)
      pixel = (pixel & 0xff000000u) | 0x00ff0000u;
  OH_Pixelmap_InitializationOptions* init = nullptr;
  OH_PixelmapInitializationOptions_Create(&init);
  if (!init)
    return nullptr;
  OH_PixelmapInitializationOptions_SetWidth(init, wide);
  OH_PixelmapInitializationOptions_SetHeight(init, high);
  OH_PixelmapInitializationOptions_SetPixelFormat(init, PIXEL_FORMAT_BGRA_8888);
  OH_PixelmapInitializationOptions_SetSrcPixelFormat(init, PIXEL_FORMAT_BGRA_8888);
  OH_PixelmapInitializationOptions_SetAlphaType(init, PIXELMAP_ALPHA_TYPE_UNPREMULTIPLIED);
  OH_PixelmapNative* map = nullptr;
  auto status = OH_PixelmapNative_CreatePixelmap(reinterpret_cast<uint8_t*>(pixels.data()),
                                                 pixels.size() * 4, init, &map);
  OH_PixelmapInitializationOptions_Release(init);
  if (status != IMAGE_SUCCESS || !map)
    return nullptr;
  napi_value result = nullptr, pixelmap = nullptr;
  napi_create_object(env, &result);
  status = OH_PixelmapNative_ConvertPixelmapNativeToNapi(env, map, &pixelmap);
  OH_PixelmapNative_Destroy(&map);
  if (status != IMAGE_SUCCESS)
    return nullptr;
  napi_set_named_property(env, result, "pixels", pixelmap);
  napi_set_named_property(env, result, "margin", ohos::js::Number(env, margin));
  napi_set_named_property(env, result, "width", ohos::js::Number(env, wide));
  napi_set_named_property(env, result, "height", ohos::js::Number(env, high));
  return result;
}
}  // namespace nativeapi::tests
