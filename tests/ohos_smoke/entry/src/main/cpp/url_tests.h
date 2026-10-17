#pragma once
#include <atomic>
#include <mutex>
#include <sstream>
#include <thread>
#include "../../../../../../src/capi/url_opener_c.h"
#include "../../../../../../src/url_opener.h"

namespace nativeapi::tests {
inline bool url_uninitialized_closed = false;
class UrlTests : public std::enable_shared_from_this<UrlTests> {
 public:
  napi_env env = nullptr;
  napi_deferred deferred = nullptr;
  uv_timer_t timer{};
  std::atomic<bool> completed{false};
  int passed = 0, failed = 0, ticks = 0;
  std::string link;
  bool use_c = false;
  std::ostringstream report;
  std::mutex report_mutex;
  void Check(bool ok, const char* text) {
    std::lock_guard<std::mutex> lock(report_mutex);
    (ok ? passed : failed)++;
    report << (ok ? "PASS " : "FAIL ") << text << '\n';
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-url", "%{public}s %{public}s",
                 ok ? "PASS" : "FAIL", text);
  }
  void Start() {
    auto& opener = UrlOpener::GetInstance();
    Check(url_uninitialized_closed, "uninitialized URL host rejects capability queries");
    Check(opener.IsSupported() && native_url_opener_is_supported(),
          "attached SDK URL dispatcher is supported through C++ and C ABI");
    Check(opener.CanOpen("https://example.com") && opener.CanOpen("http://example.com") &&
              native_url_opener_can_open("https://example.com"),
          "declared HTTP/HTTPS schemes have real system handlers");
    Check(!opener.CanOpen("") && !opener.CanOpen("example.com") &&
              !opener.CanOpen("mailto:test@example.com") && !native_url_opener_can_open(nullptr),
          "empty, missing and unsupported schemes are rejected");
    Check(!opener.CanOpen("https://") &&
              !opener.CanOpen(std::string("https://example.com\0bad", 23)) &&
              !opener.CanOpen("https://example.com/\nunsafe"),
          "SDK parsing rejects malformed links and embedded control characters");
    Check(opener.Open("").error_code == UrlOpenErrorCode::kInvalidUrlEmpty &&
              opener.Open("example.com").error_code == UrlOpenErrorCode::kInvalidUrlMissingScheme &&
              opener.Open("file:///tmp/a").error_code ==
                  UrlOpenErrorCode::kInvalidUrlUnsupportedScheme,
          "C++ validation preserves specific public error codes");
    auto ui = opener.Open("https://example.com");
    Check(!ui.success && ui.error_code == UrlOpenErrorCode::kInvocationFailed &&
              ui.error_message.find("worker") != ui.error_message.npos,
          "UI-thread Open fails explicitly without launching a handler");
    auto invalid = native_url_opener_open("mailto:test@example.com");
    Check(!invalid.success &&
              invalid.error_code == NATIVE_URL_OPEN_ERROR_CODE_INVALID_URL_UNSUPPORTED_SCHEME &&
              invalid.error_message && invalid.error_message[0],
          "generated C ABI preserves validation error and owned diagnostic");
    native_url_open_result_free(&invalid);
    OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-url", "NATIVEAPI_URL_BEGIN %{public}s",
                 link.c_str());
    auto self = shared_from_this();
    std::thread([self] {
      auto& opener = UrlOpener::GetInstance();
      self->Check(opener.IsSupported() && opener.CanOpen("  HTTPS://example.com  "),
                  "worker dispatches normalized capability queries to the UI thread");
      auto malformed = opener.Open("https://");
      self->Check(!malformed.success &&
                      malformed.error_code == UrlOpenErrorCode::kInvocationFailed &&
                      !malformed.error_message.empty(),
                  "worker malformed URL fails with an explicit diagnostic");
      if (self->use_c) {
        auto result = native_url_opener_open(self->link.c_str());
        self->Check(result.success && result.error_code == NATIVE_URL_OPEN_ERROR_CODE_NONE &&
                        (!result.error_message || !result.error_message[0]),
                    "generated C ABI observes actual system URL dispatch completion");
        native_url_open_result_free(&result);
      } else {
        auto result = opener.Open(self->link);
        if (!result.success)
          OH_LOG_Print(LOG_APP, LOG_ERROR, 1, "nativeapi-url", "SDK_FAILURE %{public}s",
                       result.error_message.c_str());
        self->Check(result.success && result.error_code == UrlOpenErrorCode::kNone &&
                        result.error_message.empty(),
                    "C++ observes actual system URL dispatch completion");
      }
      self->completed = true;
    }).detach();
  }
  void Tick() {
    if (!completed && ++ticks < 300)
      return;
    if (!completed)
      Check(false, "URL completion callback deadline");
    std::lock_guard<std::mutex> lock(report_mutex);
    const auto marker = failed ? "NATIVEAPI_URL_FAIL" : "NATIVEAPI_URL_PASS";
    report << marker << " (" << passed << " passed, " << failed << " failed)\n";
    OH_LOG_Print(LOG_APP, failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-url",
                 "%{public}s (%{public}d passed, %{public}d failed)", marker, passed, failed);
    napi_handle_scope scope;
    napi_open_handle_scope(env, &scope);
    napi_value result;
    auto text = report.str();
    napi_create_string_utf8(env, text.data(), text.size(), &result);
    napi_resolve_deferred(env, deferred, result);
    napi_close_handle_scope(env, scope);
    uv_timer_stop(&timer);
    uv_close(reinterpret_cast<uv_handle_t*>(&timer), [](uv_handle_t* handle) {
      delete static_cast<std::shared_ptr<UrlTests>*>(handle->data);
    });
  }
};
inline napi_value RunUrls(napi_env env, napi_callback_info info) {
  auto tests = std::make_shared<UrlTests>();
  tests->env = env;
  size_t count = 2;
  napi_value args[2]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  if (count)
    tests->link = ohos::js::String(env, args[0]);
  if (count > 1)
    napi_get_value_bool(env, args[1], &tests->use_c);
  napi_value promise;
  napi_create_promise(env, &tests->deferred, &promise);
  uv_loop_t* loop = nullptr;
  napi_get_uv_event_loop(env, &loop);
  uv_timer_init(loop, &tests->timer);
  tests->timer.data = new std::shared_ptr<UrlTests>(tests);
  uv_timer_start(
      &tests->timer,
      [](uv_timer_t* t) { (*static_cast<std::shared_ptr<UrlTests>*>(t->data))->Tick(); }, 100, 100);
  tests->Start();
  return promise;
}
}  // namespace nativeapi::tests
