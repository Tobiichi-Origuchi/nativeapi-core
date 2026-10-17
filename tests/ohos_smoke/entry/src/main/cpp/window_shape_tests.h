#pragma once
#include "../../../../../../src/capi/window_shape_c.h"
#include "../../../../../../src/window_shape.h"
namespace nativeapi::tests {
inline const bool shape_before_host =
    Window::IsShapeSupported() || native_window_is_shape_supported();
struct ShapeJournal {
  std::shared_ptr<Window> target, main;
  native_window_t alias = 0;
  int passed = 0, failed = 0;
  bool resize_accepted = false;
  void Check(bool ok, const char* message) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-shape", "%{public}s %{public}s",
                 ok ? "PASS" : "FAIL", message);
  }
  std::string State() {
    std::ostringstream s;
    s << "{\"ready\":" << (target ? "true" : "false")
      << ",\"alive\":" << (target && target->GetNativeObject() ? "true" : "false")
      << ",\"shaped\":" << (target && target->IsShaped() ? "true" : "false")
      << ",\"resizeAccepted\":" << (resize_accepted ? "true" : "false") << ",\"passed\":" << passed
      << ",\"failed\":" << failed << "}";
    return s.str();
  }
  std::shared_ptr<WindowShape> Polygon(const std::string& command) {
    auto shape = std::make_shared<WindowShape>();
    auto b = target->GetBounds();
    if (command == "resize-triangle" || command == "resize-probe") {
      b.width = 200;
      b.height = 140;
    }
    std::vector<Point> points;
    if (command == "concave")
      points = {{.1, .1}, {.9, .1}, {.9, .4}, {.4, .4}, {.4, .9}, {.1, .9}};
    else if (command == "bow")
      points = {{.1, .1}, {.9, .9}, {.1, .9}, {.9, .1}};
    else if (command == "outside")
      points = {{2, 2}, {3, 2}, {2, 3}};
    else if (command == "rectangle")
      points = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    else
      points = {{.1, .1}, {.9, .1}, {.1, .9}};
    for (auto p : points)
      shape->AddPoint({p.x * b.width, p.y * b.height});
    return shape;
  }
  bool Apply(std::shared_ptr<WindowShape> shape, bool use_c) {
    if (!use_c)
      return target->SetShape(shape);
    native_window_shape_t handle = 0;
    if (shape) {
      handle = native_window_shape_create();
      for (size_t i = 0; i < shape->GetPointCount(); ++i) {
        auto p = shape->GetPointAt(i);
        if (!native_window_shape_add_point(handle, {p.x, p.y})) {
          native_window_shape_free(handle);
          return false;
        }
      }
    }
    const bool ok = native_window_set_shape(alias, handle);
    native_window_shape_free(handle);
    return ok;
  }
  void Setup() {
    Check(!shape_before_host, "C++ and C capability is false before initialization");
    Check(Window::IsShapeSupported() && native_window_is_shape_supported(),
          "initialized SDK supports reversible native masks");
    Check(!Window::IsInputShapeSupported() && !native_window_is_input_shape_supported(),
          "independent arbitrary input polygons remain unsupported");
    Window invalid(reinterpret_cast<void*>(uintptr_t{123}));
    Check(!invalid.SetShape(nullptr) && !invalid.IsShaped() && !invalid.IsInputShaped(),
          "foreign window tokens return safe defaults");
    Check(!native_window_set_shape(0, 0) && !native_window_is_shaped(0),
          "empty C handle cannot mutate a native window");
    main = Application::GetInstance().GetPrimaryWindow();
    Check(main && !main->SetShape(std::make_shared<WindowShape>()) && !main->SetShape(nullptr),
          "main window mask operations are rejected");
    auto builder = native_window_shape_create();
    Check(builder && native_window_shape_get_point_count(builder) == 0,
          "C shape builder begins empty");
    Check(!native_window_shape_add_point(builder, {-1, 0}) &&
              !native_window_shape_add_point(builder, {NAN, 0}) &&
              !native_window_shape_add_point(builder, {0, INFINITY}) &&
              !native_window_shape_add_point(builder, {16385, 0}),
          "builder rejects invalid coordinates");
    Check(native_window_shape_add_point(builder, {12.5, 24.25}) &&
              native_window_shape_get_point_at(builder, 0).x == 12.5 &&
              native_window_shape_get_point_at(builder, 0).y == 24.25 &&
              native_window_shape_get_point_at(builder, 99).x == 0,
          "C builder round-trips fractional points and safe out-of-range defaults");
    native_window_shape_clear(builder);
    bool limit = true;
    for (size_t i = 0; i < 4096; ++i)
      limit = native_window_shape_add_point(builder, {0, 16384}) && limit;
    Check(limit && !native_window_shape_add_point(builder, {0, 0}) &&
              native_window_shape_get_point_count(builder) == 4096,
          "builder accepts boundary coordinates and rejects vertex 4097");
    native_window_shape_clear(builder);
    Check(native_window_shape_get_point_count(builder) == 0, "C builder clears reusable state");
    native_window_shape_free(builder);
    Check(ohos::CreateSubWindow(
              "NativeAPI Window Shape", "pages/ShapeTarget",
              [this](auto w) {
                target = w;
                Check(bool(target), "native shape child created");
                if (!target)
                  return;
                alias = native_window_manager_get(target->GetId());
                auto b = main->GetBounds();
                target->SetBounds({b.x + 80, b.y + 100, 180, 160});
                target->SetTitleBarStyle(TitleBarStyle::Hidden);
                target->SetBackgroundColor(Color::FromRGBA(255, 0, 255));
                target->SetHasShadow(false);
                target->SetCornerPreference(WindowCornerPreference::DoNotRound);
                target->Show();
                Check(alias && target->GetTitleBarStyle() == TitleBarStyle::Hidden &&
                          !target->IsShaped(),
                      "child starts undecorated and rectangular with shared C identity");
                auto invalid_shape = std::make_shared<WindowShape>();
                invalid_shape->AddPoint({0, 0});
                invalid_shape->AddPoint({5, 5});
                Check(!Apply(invalid_shape, false) && !Apply(invalid_shape, true) &&
                          !target->IsShaped(),
                      "one/two-point shapes fail without applying a mask");
                Check(!target->SetInputShape(Polygon("triangle")) &&
                          !native_window_set_input_shape(alias, 0) && !target->IsInputShaped() &&
                          !native_window_is_input_shaped(alias),
                      "input-only operations reject without changing drawing");
              }),
          "native shape child creation submitted");
  }
  bool Call(const std::string& command, bool use_c) {
    if (!target)
      return false;
    if (command == "close") {
      target->Close();
      return true;
    }
    if (command == "clear")
      return Apply(nullptr, use_c);
    if (command == "empty-clear")
      return !Apply(nullptr, use_c);  // worker observes SDK's real rejection.
    auto polygon = Polygon(command);
    const auto prior = target->IsShaped();
    const bool ok = Apply(polygon, use_c);
    if (command == "resize-probe") {
      resize_accepted = ok;
      return target->IsShaped() == ok;
    }
    polygon->Clear();  // The submitted native request owns a snapshot, never this builder.
    if (command == "pending")
      return ok && !Apply(nullptr, true) && target->IsShaped() == prior;
    return ok;
  }
  void Verify() {
    Check(target && target->IsShaped() == native_window_is_shaped(alias),
          "C++ and C share last successful native mask state");
    Check(target && !target->IsInputShaped() && !native_window_is_input_shaped(alias),
          "visual mask does not invent independent input-shape state");
  }
};
inline ShapeJournal shapes;
inline napi_value ShapeState(napi_env env, napi_callback_info info) {
  size_t count = 1;
  napi_value arg = nullptr;
  napi_get_cb_info(env, info, &count, &arg, nullptr, nullptr);
  auto command = count ? ohos::js::String(env, arg) : std::string();
  if (command == "setup")
    shapes.Setup();
  if (command == "verify")
    shapes.Verify();
  if (command == "finish") {
    shapes.Check(shapes.target && !shapes.target->GetNativeObject() && !shapes.target->IsShaped() &&
                     !native_window_is_shaped(shapes.alias) && !shapes.Apply(nullptr, false) &&
                     !shapes.Apply(nullptr, true),
                 "retained closed wrappers reject both ABI mutations and reset getters");
    native_window_free(shapes.alias);
    shapes.alias = 0;
    OH_LOG_Print(LOG_APP, shapes.failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-shape",
                 "NATIVEAPI_SHAPE_FINAL_%{public}s passed=%{public}d failed=%{public}d",
                 shapes.failed ? "FAIL" : "PASS", shapes.passed, shapes.failed);
  }
  return ohos::String(env, shapes.State());
}
inline napi_value ShapeAction(napi_env env, napi_callback_info info) {
  size_t count = 3;
  napi_value args[3]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  struct Work {
    napi_deferred deferred{};
    napi_async_work work{};
    std::string command;
    bool c = false, worker = false, result = false;
  };
  auto job = std::make_unique<Work>();
  job->command = ohos::js::String(env, args[0]);
  napi_get_value_bool(env, args[1], &job->c);
  napi_get_value_bool(env, args[2], &job->worker);
  napi_value promise = nullptr, name = nullptr;
  if (napi_create_promise(env, &job->deferred, &promise) != napi_ok)
    return nullptr;
  if (!job->worker) {
    shapes.Check(shapes.Call(job->command, job->c),
                 "UI shape request submits or overlap is safely rejected");
    napi_resolve_deferred(env, job->deferred, ohos::String(env, shapes.State()));
    return promise;
  }
  napi_create_string_utf8(env, "nativeapi.window.shape", NAPI_AUTO_LENGTH, &name);
  if (napi_create_async_work(
          env, nullptr, name,
          [](napi_env, void* p) {
            auto* j = static_cast<Work*>(p);
            j->result = shapes.Call(j->command, j->c);
          },
          [](napi_env env, napi_status status, void* p) {
            std::unique_ptr<Work> j(static_cast<Work*>(p));
            shapes.Check(
                status == napi_ok && j->result,
                "worker shape request dispatches successfully; SDK state is checked afterwards");
            napi_resolve_deferred(env, j->deferred, ohos::String(env, shapes.State()));
            napi_delete_async_work(env, j->work);
          },
          job.get(), &job->work) != napi_ok ||
      napi_queue_async_work(env, job->work) != napi_ok) {
    if (job->work)
      napi_delete_async_work(env, job->work);
    shapes.Check(false, "worker shape submission failed");
    napi_resolve_deferred(env, job->deferred, ohos::String(env, shapes.State()));
    return promise;
  }
  job.release();
  return promise;
}
}  // namespace nativeapi::tests
