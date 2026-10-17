#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <functional>
#include <vector>
#include "../src/shortcut_manager.h"

namespace {
bool refuse_global_preflight = false;
int global_preflights = 0;
bool refuse_removal = false, refuse_registration = false, dispatcher_works = true;
std::vector<std::function<void()>> pending;
}
namespace nativeapi {
bool RunOnMainThread(std::function<void()> fn) {
  if (!dispatcher_works) return false;
  pending.push_back(std::move(fn)); return true;
}
class ShortcutManagerImpl final : public ShortcutManager::Impl {
 public:
  bool IsSupported() override { return true; }
  bool CanRegister(const std::string&) override { ++global_preflights; return !refuse_global_preflight; }
  bool RegisterShortcut(const std::shared_ptr<Shortcut>&) override { return !refuse_registration; }
  bool UnregisterShortcut(const std::shared_ptr<Shortcut>&) override { return !refuse_removal; }
  void SetupEventMonitoring() override {}
  void CleanupEventMonitoring() override {}
};
ShortcutManager::ShortcutManager()
    : pimpl_(std::make_unique<ShortcutManagerImpl>()), next_shortcut_id_(1), enabled_(true) {}
ShortcutManager::~ShortcutManager() { ShutdownEmitter(); UnregisterAll(); }
}
int main() {
  auto& manager = nativeapi::ShortcutManager::GetInstance();
  int callbacks = 0;
  auto a = manager.Register("Ctrl+F9", [&] { ++callbacks; });
  auto b = manager.Register("Ctrl+F10", [] {});
  assert(a && b && manager.GetAll().size() == 2);
  refuse_removal = true;
  assert(!manager.Unregister(a->GetId()));
  assert(manager.Get(a->GetId()) == a && manager.Get("Ctrl+F9") == a);
  a->Invoke();
  assert(callbacks == 1 && a->GetCallback());
  assert(manager.UnregisterAll() == 0 && manager.GetAll().size() == 2);
  refuse_removal = false;
  assert(manager.Unregister(a->GetId()));
  assert(!manager.Get(a->GetId()) && !a->GetCallback());
  a->Invoke();
  assert(callbacks == 1);
  assert(manager.UnregisterAll() == 1 && manager.GetAll().empty());
  auto existing = manager.Register("Ctrl+F11", [] {});
  int failures = 0;
  const auto observer = manager.AddListener<nativeapi::ShortcutRegistrationFailedEvent>([&](const auto&) {
    ++failures;
    assert(manager.GetAll().size() == 1);  // Re-entry also works with no dispatcher.
  });
  dispatcher_works = false;
  assert(!manager.Register("not a valid accelerator", [] {}));
  assert(!manager.Register("Ctrl+F11", [] {}));
  refuse_registration = true;
  assert(!manager.Register("Ctrl+F12", [] {}));
  assert(failures == 3 && manager.Get(existing->GetId()) == existing);
  dispatcher_works = true;
  refuse_registration = false;
  assert(manager.RemoveListener(observer) && manager.UnregisterAll() == 1);
  // Application-local shortcuts must not reserve or probe a system-wide chord.
  refuse_global_preflight = true;
  assert(!manager.Register("Ctrl+F9", [] {}));
  const int probes = global_preflights;
  nativeapi::ShortcutOptions local;
  local.accelerator = "Ctrl+F9";
  local.scope = nativeapi::ShortcutScope::Application;
  auto app = manager.Register(local);
  assert(app && global_preflights == probes);
  assert(!manager.Register(local) && global_preflights == probes);
  assert(manager.Unregister(app->GetId()));
  refuse_registration = true;
  assert(!manager.Register(local) && manager.GetAll().empty());
  assert(global_preflights == probes);
  refuse_global_preflight = refuse_registration = false;
  auto work = std::move(pending);
  for (auto& fn : work) fn();
}
