#pragma once

#include <mutex>
#include <vector>

namespace nativeapi::tests {
// Installed once at module startup, before any native observer is registered.
// Normal work uses the real OHOS scheduler. Holding only the test queue lets
// SceneBoard finish a transition while real SDK callbacks accumulate.
struct DisplayTestDispatcher {
  std::mutex mutex;
  bool holding = false;
  std::vector<std::function<void()>> queued;

  bool Post(std::function<void()> work) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (holding) {
        queued.push_back(std::move(work));
        return true;
      }
    }
    return ohos::Post(std::move(work));
  }
  void Hold() {
    std::lock_guard<std::mutex> lock(mutex);
    holding = true;
  }
  void Drain() {
    std::vector<std::function<void()>> work;
    {
      std::lock_guard<std::mutex> lock(mutex);
      holding = false;
      work.swap(queued);
    }
    for (auto& task : work)
      task();
  }
};
inline DisplayTestDispatcher display_test_dispatcher;
inline void InstallDisplayTestDispatcher() {
  SetMainThreadDispatcher(
      [](std::function<void()> work) { return display_test_dispatcher.Post(std::move(work)); },
      nullptr);
}
}  // namespace nativeapi::tests
