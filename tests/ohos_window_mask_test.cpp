#include "../src/platform/ohos/ohos_window_mask.h"
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace nativeapi;
int main() {
  int checked = 0;
  auto check = [&](bool ok) {
    if (!ok)
      std::abort();
    ++checked;
  };
  WindowShape shape;
  for (auto p : {Point{1.25, 1.5}, Point{7.25, 1.5}, Point{7.25, 3.5}, Point{4.25, 3.5},
                 Point{4.25, 7.5}, Point{1.25, 7.5}})
    check(shape.AddPoint(p));
  std::vector<uint8_t> mask;
  // Independent ray-crossing oracle covers concavity, fractional coordinates and clipped edges.
  auto verify = [&](const WindowShape& polygon, uint32_t w, uint32_t h, double scale) {
    check(ohos::RasterizeWindowMask(polygon, w, h, scale, mask));
    for (uint32_t y = 0; y < h; ++y)
      for (uint32_t x = 0; x < w; ++x) {
        bool inside = false;
        auto previous = polygon.GetPointAt(polygon.GetPointCount() - 1);
        const double px = (x + 0.5) / scale, py = (y + 0.5) / scale;
        for (size_t i = 0; i < polygon.GetPointCount(); ++i) {
          auto current = polygon.GetPointAt(i);
          if ((current.y > py) != (previous.y > py) &&
              px < (previous.x - current.x) * (py - current.y) / (previous.y - current.y) +
                       current.x)
            inside = !inside;
          previous = current;
        }
        check(mask[size_t(y) * w + x] == (inside ? 255 : 0));
      }
  };
  for (double scale : {1.0, 1.9, 3.0})
    verify(shape, 19, 17, scale);
  WindowShape bow;
  for (auto p : {Point{0.25, 0}, Point{9.25, 9}, Point{0.25, 9}, Point{9.25, 0}})
    bow.AddPoint(p);
  verify(bow, 14, 13, 1.9);
  WindowShape degenerate;
  for (auto p : {Point{2, 2}, Point{2, 2}, Point{2, 2}})
    degenerate.AddPoint(p);
  verify(degenerate, 10, 10, 1);
  check(!ohos::RasterizeWindowMask(WindowShape{}, 10, 10, 1, mask));
  check(!ohos::RasterizeWindowMask(shape, 0, 10, 1, mask));
  check(!ohos::RasterizeWindowMask(shape, 100000, 100000, 1, mask));
  check(!ohos::RasterizeWindowMask(shape, 16384, 16384, 1, mask));
  check(!ohos::RasterizeWindowMask(shape, 10, 10, 0, mask));
  check(!ohos::RasterizeWindowMask(shape, 10, 10, std::numeric_limits<double>::infinity(), mask));
  std::cout << "PASS " << checked << " raster samples and validation cases\n";
}
