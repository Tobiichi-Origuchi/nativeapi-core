#pragma once
#include "../../../../../../src/capi/window_shadow_c.h"
#include "../../../../../../src/capi/window_shape_c.h"
#include "../../../../../../src/platform/ohos/ohos_window_shadow.h"
#include "../../../../../../src/window_shadow.h"
namespace nativeapi::tests {
struct WindowShadowJournal {
  std::shared_ptr<Window> target;
  native_window_t c = 0;
  size_t registered_windows = 2;
  int passed = 0, failed = 0;
  void Check(bool ok, const char* label) {
    (ok ? passed : failed)++;
    if (!ok)
      OH_LOG_Print(LOG_APP, LOG_ERROR, 1, "nativeapi-shadow", "FAIL %{public}s", label);
  }
  std::string State(bool verify) {
    auto config = target ? target->GetCustomShadow() : nullptr;
    if (verify && target) {
      auto cc = native_window_get_custom_shadow(c);
      Check(bool(config) == bool(cc), "C++ and C custom shadow presence");
      if (config && cc) {
        Check(config->GetColor().ToRGBA() ==
                      native_color_to_rgba(native_window_shadow_get_color(cc)) &&
                  config->GetBlurRadius() == native_window_shadow_get_blur_radius(cc),
              "C++ and C shadow configuration values");
        auto point = native_window_shadow_get_offset(cc);
        Check(point.x == config->GetOffset().x && point.y == config->GetOffset().y,
              "C++ and C shadow offsets");
        auto before = config->GetColor().ToRGBA();
        config->SetColor(Color::White);
        native_window_shadow_set_color(cc, {0, 255, 0, 255});
        Check(target->GetCustomShadow()->GetColor().ToRGBA() == before,
              "C++ and C getters return independent copies");
      }
      native_window_shadow_free(cc);
      Check(target->HasShadow() == native_window_has_shadow(c), "visibility switch aliases agree");
      if (target->GetNativeObject())
        Check(WindowManager::GetInstance().GetAll().size() == registered_windows,
              "private helper never enters nativeapi window identities");
      if (!target->GetNativeObject()) {
        Check(!target->SetCustomShadow(std::make_shared<WindowShadow>()) &&
                  !native_window_set_custom_shadow(c, 0),
              "destroyed wrappers reject shadow mutation");
        Check(!target->GetCustomShadow(), "destroyed wrapper returns no shadow config");
      }
    }
    config = target ? target->GetCustomShadow() : nullptr;
    std::ostringstream out;
    out << "{\"passed\":" << passed << ",\"failed\":" << failed
        << ",\"configured\":" << (config ? "true" : "false")
        << ",\"enabled\":" << (target && target->HasShadow() ? "true" : "false")
        << ",\"alive\":" << (target && target->GetNativeObject() ? "true" : "false")
        << ",\"color\":" << (config ? config->GetColor().ToRGBA() : 0)
        << ",\"blur\":" << (config ? config->GetBlurRadius() : 0);
    auto p = config ? config->GetOffset() : Point{};
    out << ",\"offset\":[" << p.x << ',' << p.y << "]}";
    return out.str();
  }
};
inline WindowShadowJournal shadow_journal;
inline napi_value AttachShadowHost(napi_env env, napi_callback_info info) {
  size_t count = 1;
  napi_value host = nullptr;
  napi_get_cb_info(env, info, &count, &host, nullptr, nullptr);
  return ohos::Boolean(env, count == 1 && ohos::AttachWindowShadowHost(env, host));
}
inline napi_value ShadowTargetAttach(napi_env env, napi_callback_info info) {
  size_t count = 2;
  napi_value args[2]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  bool primary = false;
  if (count > 1) napi_get_value_bool(env, args[1], &primary);
  auto main = Application::GetInstance().GetPrimaryWindow();
  if (!count || !main)
    return nullptr;
  auto target = ohos::AttachWindow(env, args[0],
                                 primary ? IdAllocator::kInvalidId : main->GetId(), primary);
  shadow_journal.registered_windows = primary ? 1 : 2;
  if (primary) shadow_journal.Check(target == main, "main target preserves primary window identity");
  if (target)
    target->SetTitleBarStyle(TitleBarStyle::Hidden);
  shadow_journal.target = target;
  shadow_journal.c = target ? native_window_manager_get(target->GetId()) : 0;
  shadow_journal.Check(target && shadow_journal.c,
                       "actual SDK target attached through owning C alias");
  Window empty;
  auto foreign = std::make_shared<Window>(reinterpret_cast<void*>(uintptr_t{123}));
  shadow_journal.Check(!empty.SetCustomShadow(std::make_shared<WindowShadow>()) &&
                           !foreign->SetCustomShadow(nullptr),
                       "empty and foreign wrappers reject configurations");
  return ohos::String(env, shadow_journal.State(false));
}
inline napi_value ShadowState(napi_env env, napi_callback_info info) {
  size_t count = 1;
  napi_value arg = nullptr;
  napi_get_cb_info(env, info, &count, &arg, nullptr, nullptr);
  const auto action = count ? ohos::js::String(env, arg) : "";
  if (action == "finish") {
    native_window_free(shadow_journal.c);
    shadow_journal.c = 0;
    shadow_journal.target.reset();
  }
  return ohos::String(env, shadow_journal.State(action == "verify"));
}
struct WindowShadowWork {
  napi_async_work work = nullptr;
  napi_deferred deferred = nullptr;
  std::string action;
  bool c = false, worker = false, result = false;
  void Run() {
    auto window = shadow_journal.target;
    if (!window)
      return;
    shadow_journal.Check(IsMainThread() != worker, "shadow action runs on requested real thread");
    if (action == "title-hidden" || action == "title-normal") {
      window->SetTitleBarStyle(action == "title-hidden" ? TitleBarStyle::Hidden : TitleBarStyle::Normal);
      result = true;
    } else if (action == "enable" || action == "disable") {
      if (c)
        native_window_set_has_shadow(shadow_journal.c, action == "enable");
      else
        window->SetHasShadow(action == "enable");
      result = true;
    } else if (action == "shape" || action == "outside" || action == "clear-shape") {
      std::shared_ptr<WindowShape> shape;
      if (action != "clear-shape") {
        shape = std::make_shared<WindowShape>();
        const auto size = window->GetContentSize();
        if (action == "shape") {
          shape->AddPoint({0, 0});
          shape->AddPoint({size.width, 0});
          shape->AddPoint({0, size.height});
        } else {
          shape->AddPoint({size.width + 100, size.height + 100});
          shape->AddPoint({size.width + 200, size.height + 100});
          shape->AddPoint({size.width + 200, size.height + 200});
        }
      }
      result = window->SetShape(shape);
    } else if (action == "clear")
      result = c ? native_window_set_custom_shadow(shadow_journal.c, 0)
                 : window->SetCustomShadow(nullptr);
    else {
      auto value = std::make_shared<WindowShadow>();
      value->SetBlurRadius(action == "soft" ? 18 : 0);
      value->SetOffset({12, 8});
      value->SetColor(action == "green" ? Color{0, 255, 0, 192} : Color{255, 0, 0, 192});
      if (c) {
        auto h = native_window_shadow_create();
        native_window_shadow_set_color(h, to_c_color(value->GetColor()));
        native_window_shadow_set_blur_radius(h, value->GetBlurRadius());
        native_window_shadow_set_offset(h, {12, 8});
        result = native_window_set_custom_shadow(shadow_journal.c, h);
        native_window_shadow_free(h);
      } else
        result = window->SetCustomShadow(value);
      // The setter copies; mutating/dropping the caller's value cannot recolor it.
      value->SetColor(Color::White);
      value->SetBlurRadius(64);
      value->SetOffset({-64, -64});
    }
  }
};
inline napi_value ShadowAction(napi_env env, napi_callback_info info) {
  size_t count = 3;
  napi_value args[3]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  if (count != 3)
    return nullptr;
  auto job = std::make_unique<WindowShadowWork>();
  job->action = ohos::js::String(env, args[0]);
  napi_get_value_bool(env, args[1], &job->c);
  napi_get_value_bool(env, args[2], &job->worker);
  napi_value promise = nullptr;
  napi_create_promise(env, &job->deferred, &promise);
  if (!job->worker) {
    job->Run();
    napi_resolve_deferred(env, job->deferred, ohos::Boolean(env, job->result));
    return promise;
  }
  auto name = ohos::String(env, "nativeapi.shadow.action");
  if (napi_create_async_work(
          env, nullptr, name, [](napi_env, void* p) { static_cast<WindowShadowWork*>(p)->Run(); },
          [](napi_env env, napi_status status, void* p) {
            std::unique_ptr<WindowShadowWork> job(static_cast<WindowShadowWork*>(p));
            napi_resolve_deferred(env, job->deferred,
                                  ohos::Boolean(env, status == napi_ok && job->result));
            napi_delete_async_work(env, job->work);
          },
          job.get(), &job->work) != napi_ok ||
      napi_queue_async_work(env, job->work) != napi_ok) {
    if (job->work)
      napi_delete_async_work(env, job->work);
    napi_reject_deferred(env, job->deferred, ohos::String(env, "shadow action submission failed"));
    return promise;
  }
  job.release();
  return promise;
}
}  // namespace nativeapi::tests
