// Shared reconciliation with a controlled platform seam, not a native SDK test.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <functional>
#include <iostream>
#include "../src/display_manager.h"

namespace {
bool fail_enumeration = false;
std::vector<int> connected{1};
std::function<void()> native_notice;
}  // namespace
namespace nativeapi {
class Display::Impl {
 public:
  explicit Impl(void* token) : token(token) {}
  void* token;
  DisplayId id = IdAllocator::Allocate<Display>();
};
Display::Display(void* token) : pimpl_(std::make_unique<Impl>(token)) {}
Display::~Display() = default;
DisplayId Display::GetId() const {
  return pimpl_->id;
}
void* Display::GetNativeObjectInternal() const {
  return pimpl_->token;
}
DisplayManager::DisplayManager() = default;
DisplayManager::~DisplayManager() {
  ShutdownEmitter();
}
void DisplayManager::StartEventListening() {
  native_notice = [this] { HandleDisplaysChanged(); };
}
void DisplayManager::StopEventListening() {
  native_notice = {};
}
Point DisplayManager::GetCursorPosition() {
  return {};
}
bool DisplayManager::EnumerateNativeDisplays(std::vector<NativeDisplayInfo>& result) {
  if (fail_enumeration) {
    result.push_back({"partial-invalid-result", reinterpret_cast<void*>(99), true});
    return false;
  }
  for (auto id : connected)
    result.push_back({std::to_string(id), reinterpret_cast<void*>(uintptr_t(id)), id == 1});
  return true;
}
}  // namespace nativeapi
int main() {
  using namespace nativeapi;
  auto& manager = DisplayManager::GetInstance();
  auto original = manager.GetPrimary();
  assert(original && manager.GetAll().at(0) == original);
  int added = 0, removed = 0;
  std::shared_ptr<Display> removed_display;
  auto listener = manager.AddListener<DisplayEvent>([&](const auto& event) {
    if (dynamic_cast<const DisplayAddedEvent*>(&event))
      ++added;
    if (dynamic_cast<const DisplayRemovedEvent*>(&event)) {
      ++removed;
      removed_display = event.GetDisplay();
    }
  });
  fail_enumeration = true;
  assert(manager.GetAll().empty());
  assert(!manager.GetPrimary());
  native_notice();
  assert(added == 0 && removed == 0);
  fail_enumeration = false;
  auto recovered = manager.GetPrimary();
  assert(recovered == original && recovered->GetId() == original->GetId());
  native_notice();
  assert(added == 0 && removed == 0);
  connected.clear();
  native_notice();
  assert(manager.GetAll().empty() && !manager.GetPrimary());
  assert(removed == 1 && added == 0 && removed_display == original);
  connected = {1};
  native_notice();
  auto reconnected = manager.GetPrimary();
  assert(reconnected && reconnected != original && reconnected->GetId() != original->GetId());
  assert(added == 1 && removed == 1);
  assert(manager.RemoveListener(listener));
  assert(!native_notice);
  std::cout << "PASS failed/partial enumeration preserves identity and emits no false disconnects; "
               "real empty topology and reconnect remain distinct\n";
}
