#include "../src/platform/ohos/ohos_shadow_raster.h"
#include <iostream>
#include <limits>
int main() {
  using namespace nativeapi;
  int failed = 0, passed = 0;
  auto check = [&](bool ok, const char* label) {
    if (ok)
      ++passed;
    else {
      ++failed;
      std::cerr << "FAIL " << label << '\n';
    }
  };
  WindowShadow options;
  options.SetBlurRadius(0);
  options.SetOffset({2, 1});
  options.SetColor({12, 100, 200, 128});
  WindowShape rectangle;
  auto hard = ohos::RasterizeWindowShadow(10, 8, 1, rectangle, options);
  auto alpha = [&](int x, int y) {
    return hard.rgba[(size_t(y + hard.margin) * hard.width + x + hard.margin) * 4 + 3];
  };
  check(hard.width == 18 && hard.height == 16 && hard.margin == 4,
        "hard shadow support and dimensions");
  check(alpha(5, 4) == 0 && alpha(9, 7) == 0, "visible target interior always transparent");
  check(alpha(10, 4) == 128 && alpha(11, 4) == 128 && alpha(12, 4) == 0,
        "hard offset shadow exterior has exact alpha");
  const auto index = (size_t(4 + hard.margin) * hard.width + 10 + hard.margin) * 4;
  check(hard.rgba[index] == 12 && hard.rgba[index + 1] == 100 && hard.rgba[index + 2] == 200,
        "straight color channels preserve non-primary colors");
  WindowShape outside;
  outside.AddPoint({20, 20});
  outside.AddPoint({30, 20});
  outside.AddPoint({30, 30});
  auto absent = ohos::RasterizeWindowShadow(10, 8, 1, outside, options);
  bool transparent = true;
  for (size_t i = 3; i < absent.rgba.size(); i += 4)
    transparent = transparent && absent.rgba[i] == 0;
  check(transparent, "off-window contour cannot produce ghost shadow");
  WindowShape clipped;
  clipped.AddPoint({0, 0});
  clipped.AddPoint({20, 0});
  clipped.AddPoint({20, 20});
  clipped.AddPoint({0, 20});
  auto full = ohos::RasterizeWindowShadow(10, 8, 1, clipped, options);
  check(full.rgba == hard.rgba, "clipped surrounding polygon equals actual visible rectangle");
  options.SetBlurRadius(6);
  auto soft = ohos::RasterizeWindowShadow(30, 25, 2, rectangle, options);
  bool colored = false;
  for (size_t i = 3; i < soft.rgba.size(); i += 4)
    colored = colored || (soft.rgba[i] > 0 && soft.rgba[i] < 128);
  check(colored, "soft shadow has actual partial alpha exterior");
  for (double scale : {0.0, -1.0, 9.0, std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()})
    check(ohos::RasterizeWindowShadow(10, 8, scale, rectangle, options).rgba.empty(),
          "invalid scale rejected");
  check(ohos::RasterizeWindowShadow(16384, 16384, 1, rectangle, options).rgba.empty(),
        "raster memory bound");
  check(ohos::RasterizeWindowShadow(0, 8, 1, rectangle, options).rgba.empty(),
        "invalid dimensions rejected");
  std::cout << "OHOS shadow raster: " << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
