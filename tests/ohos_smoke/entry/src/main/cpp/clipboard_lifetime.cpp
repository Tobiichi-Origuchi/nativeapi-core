// Separate, symbol-bound copy of core: a real ArkTS worker owns its N-API
// environment without changing the application's UI-host singleton.
#include <hilog/log.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>
#include "../../../../../../src/clipboard.h"
#include "../../../../../../src/platform/ohos/ohos_runtime.h"

namespace {
using namespace nativeapi;
struct Epoch {
  uint64_t generation = 0;
  bool pending = false;
  std::atomic<int> calls{0}, dropped{0}, notices{0};
};
std::shared_ptr<Epoch> current;
int passed = 0, failed = 0, cleaned = 0;
uint64_t last_generation = 0;
std::mutex queue_mutex;
std::condition_variable queued_notice;
std::vector<std::function<void()>> notices;
void DrainNotices() {
  decltype(notices) work;
  {
    std::lock_guard<std::mutex> lock(queue_mutex);
    work.swap(notices);
  }
  for (auto& task : work)
    task();
}
void Check(bool ok, const char* message) {
  ++(ok ? passed : failed);
  OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-clipboard-lifetime",
               "%{public}s %{public}s", ok ? "PASS" : "FAIL", message);
}
struct ReadToken {
  std::shared_ptr<Epoch> epoch;
  ~ReadToken() { ++epoch->dropped; }
};
void VerifyCleanup(void* data) {
  std::unique_ptr<std::shared_ptr<Epoch>> held(static_cast<std::shared_ptr<Epoch>*>(data));
  const auto& e = **held;
  Check(!ohos::IsInitialized() && ohos::EnvironmentGeneration() == 0,
        "actual worker environment cleanup disables old runtime dispatch");
  Check(!Clipboard::GetInstance().IsMonitoring() &&
            Clipboard::GetInstance().GetTotalListenerCount() == 0,
        "actual environment cleanup removes native clipboard subscriptions and listeners");
  // The N-API abort finalizer may dispose TSFN items after env cleanup hooks.
  // Verify the final released count from the next main-thread snapshot instead.
  Check(e.calls.load() == (e.pending ? 0 : 30),
        "terminating an environment never invokes its queued clipboard reads");
  ++cleaned;
}
napi_value Begin(napi_env env, napi_callback_info info) {
  napi_value arg{};
  size_t count = 1;
  bool pending = false;
  napi_get_cb_info(env, info, &count, &arg, nullptr, nullptr);
  if (count)
    napi_get_value_bool(env, arg, &pending);
  Check(!ohos::IsInitialized() && !Clipboard::GetInstance().IsMonitoring(),
        "replacement worker starts with an inactive clipboard runtime");
  auto previous = current;
  current = std::make_shared<Epoch>();
  current->pending = pending;
  auto* cleanup = new std::shared_ptr<Epoch>(current);
  // Hooks run in reverse registration order; this assertion follows core's hook.
  Check(napi_add_env_cleanup_hook(env, VerifyCleanup, cleanup) == napi_ok,
        "actual worker verification cleanup hook installed");
  Check(ohos::Initialize(env), "isolated clipboard host initializes in actual worker");
  current->generation = ohos::EnvironmentGeneration();
  Check(current->generation > last_generation,
        "replacement environment has a new native generation");
  last_generation = current->generation;
  auto epoch = current;
  Clipboard::GetInstance().AddListener<ClipboardChangedEvent>(
      [epoch](const auto&) { ++epoch->notices; });
  Check(Clipboard::GetInstance().IsMonitoring(),
        "actual worker owns a real pasteboard subscription");
  if (previous) {
    DrainNotices();
    Check(previous->notices == 0 && epoch->notices == 0,
          "actual old-environment SDK notices cannot enter a replacement worker subscription");
  }
  SetMainThreadDispatcher(
      [](auto work) {
        {
          std::lock_guard<std::mutex> lock(queue_mutex);
          notices.push_back(std::move(work));
        }
        queued_notice.notify_one();
        return true;
      },
      nullptr);
  Check(Clipboard::GetInstance().WriteText("nativeapi-lifetime-notice"),
        "actual public pasteboard publication generates an environment-bound notice");
  {
    std::unique_lock<std::mutex> lock(queue_mutex);
    Check(queued_notice.wait_for(lock, std::chrono::seconds(5), [] { return !notices.empty(); }),
          "actual SDK observer work is queued before environment disposal");
  }
  for (int i = 0; i < 30; ++i) {
    auto token = std::make_shared<ReadToken>();
    token->epoch = epoch;
    Clipboard::GetInstance().ReadText([token](bool, auto) { ++token->epoch->calls; });
  }
  return nullptr;
}
napi_value Summary(napi_env env, napi_callback_info) {
  if (current) {
    Check(current->dropped == 30,
          "all thirty queued native read captures are released after actual environment disposal");
    Check(!Clipboard::GetInstance().IsMonitoring(),
          "disposed worker leaves no active native observer");
  }
  std::ostringstream out;
  out << "{\"ok\":true,\"passed\":" << passed << ",\"failed\":" << failed
      << ",\"cleaned\":" << cleaned << ",\"generation\":" << last_generation
      << ",\"callbacks\":" << (current ? current->calls.load() : -1)
      << ",\"released\":" << (current ? current->dropped.load() : -1) << '}';
  napi_value result;
  const auto text = out.str();
  napi_create_string_utf8(env, text.c_str(), text.size(), &result);
  return result;
}
napi_value Finish(napi_env env, napi_callback_info info) {
  DrainNotices();
  Check(current && current->notices == 0,
        "actual SDK notices stay cancelled after final environment disposal");
  return Summary(env, info);
}
napi_value Init(napi_env env, napi_value exports) {
  napi_property_descriptor methods[] = {
      {"begin", nullptr, Begin, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"summary", nullptr, Summary, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"finish", nullptr, Finish, nullptr, nullptr, nullptr, napi_default, nullptr}};
  napi_define_properties(env, exports, 3, methods);
  return exports;
}
napi_module module = {1, 0, nullptr, Init, "nativeapi_clipboard_lifetime", nullptr, {0}};
}  // namespace
extern "C" __attribute__((constructor)) void RegisterClipboardLifetime() {
  napi_module_register(&module);
}
