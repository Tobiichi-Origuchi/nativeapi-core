#pragma once
namespace nativeapi::tests {
struct DragJournal {
  struct Row {
    std::shared_ptr<Window> window;
    native_window_t alias = 0;
    size_t object_listener = 0, manager_listener = 0;
    native_listener_id_t c_listener = 0;
    int objects = 0, managers = 0, c_events = 0, downs = 0;
    bool valid = true;
  } main, child;
  int passed = 0, failed = 0;
  void Check(bool ok, const char* message) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-drag", "%{public}s %{public}s",
                 ok ? "PASS" : "FAIL", message);
  }
  void Watch(Row& row) {
    row.alias = native_window_manager_get(row.window->GetId());
    row.object_listener = row.window->AddListener<WindowMovedEvent>([&row](const auto& event) {
      ++row.objects;
      row.valid &= IsMainThread() && event.GetWindowId() == row.window->GetId();
    });
    row.manager_listener =
        WindowManager::GetInstance().AddListener<WindowMovedEvent>([&row](const auto& event) {
          if (event.GetWindowId() == row.window->GetId())
            ++row.managers;
        });
    row.c_listener = native_window_add_listener(
        row.alias,
        [](const native_window_event_t* event, void* data) {
          auto& r = *static_cast<Row*>(data);
          if (event->type == NATIVE_WINDOW_EVENT_TYPE_MOVED) {
            ++r.c_events;
            r.valid &= IsMainThread();
          }
        },
        &row, nullptr);
    Check(row.alias && row.object_listener && row.manager_listener && row.c_listener,
          "C++ object/manager and C ABI observe one native window identity");
  }
  void Setup() {
    Window invalid(reinterpret_cast<void*>(uintptr_t{123}));
    invalid.StartDragging();
    Window{}.StartDragging();
    native_window_start_dragging(0);
    Check(!invalid.GetNativeObject() && invalid.GetBounds().width == 0,
          "foreign tokens and empty C/C++ handles ignore dragging safely");
    main.window = Application::GetInstance().GetPrimaryWindow();
    Check(bool(main.window), "primary window is attached");
    if (!main.window)
      return;
    main.window->SetMovable(false);
    Watch(main);
    Check(ohos::CreateSubWindow("NativeAPI Drag Target", "pages/DragTarget",
                                [this](auto w) {
                                  child.window = w;
                                  Check(bool(w), "real native drag child created");
                                  if (!w)
                                    return;
                                  auto b = main.window->GetBounds();
                                  w->SetBounds({b.x + 80, b.y + 260, 220, 130});
                                  w->SetTitleBarStyle(TitleBarStyle::Hidden);
                                  w->SetMovable(false);
                                  w->SetHasShadow(false);
                                  w->SetBackgroundColor(Color::FromRGBA(255, 0, 255));
                                  Watch(child);
                                  w->Show();
                                }),
          "native child creation submitted");
  }
  void Verify(Row& row) {
    auto cpp = row.window->GetBounds();
    auto c = native_window_get_bounds(row.alias);
    Check(row.window->GetNativeObject() && row.valid && cpp.x == c.x && cpp.y == c.y &&
              cpp.width == c.width && cpp.height == c.height,
          "C++/C geometry and event identities agree after actual input");
    Check(row.objects == row.managers && row.objects == row.c_events,
          "real native move observations reach object/manager/C listeners equally");
    Check(!row.window->IsMovable(), "explicit moving does not change native-title dragging policy");
  }
  std::string State() {
    std::ostringstream s;
    s << "{\"ready\":" << (child.window ? "true" : "false")
      << ",\"alive\":" << (child.window && child.window->GetNativeObject() ? "true" : "false")
      << ",\"mainMoves\":" << main.objects << ",\"childMoves\":" << child.objects
      << ",\"mainDowns\":" << main.downs << ",\"childDowns\":" << child.downs
      << ",\"passed\":" << passed << ",\"failed\":" << failed << "}";
    return s.str();
  }
  void Unwatch(Row& row) {
    Check(row.window->RemoveListener(row.object_listener) &&
              WindowManager::GetInstance().RemoveListener(row.manager_listener) &&
              native_window_remove_listener(row.alias, row.c_listener),
          "all drag observation paths removed");
    native_window_free(row.alias);
    row.alias = 0;
  }
};
inline DragJournal dragging;
inline napi_value DragState(napi_env env, napi_callback_info info) {
  size_t count = 1;
  napi_value value = nullptr;
  napi_get_cb_info(env, info, &count, &value, nullptr, nullptr);
  auto command = count ? ohos::js::String(env, value) : std::string();
  if (command == "setup")
    dragging.Setup();
  if (command == "verify-child")
    dragging.Verify(dragging.child);
  if (command == "verify-main")
    dragging.Verify(dragging.main);
  if (command == "hide-child")
    dragging.child.window->Hide();
  if (command == "close")
    dragging.child.window->Close();
  if (command == "finish") {
    dragging.child.window->StartDragging();
    native_window_start_dragging(dragging.child.alias);
    dragging.Check(!dragging.child.window->GetNativeObject() &&
                       !native_window_get_native_object(dragging.child.alias),
                   "closed retained C++/C wrappers ignore dragging");
    dragging.Unwatch(dragging.child);
    dragging.Unwatch(dragging.main);
    OH_LOG_Print(LOG_APP, dragging.failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-drag",
                 "NATIVEAPI_DRAG_FINAL_%{public}s passed=%{public}d failed=%{public}d",
                 dragging.failed ? "FAIL" : "PASS", dragging.passed, dragging.failed);
  }
  return ohos::String(env, dragging.State());
}
inline napi_value DragStart(napi_env env, napi_callback_info info) {
  size_t count = 2;
  napi_value values[2]{};
  napi_get_cb_info(env, info, &count, values, nullptr, nullptr);
  bool main = false, use_c = false;
  napi_get_value_bool(env, values[0], &main);
  napi_get_value_bool(env, values[1], &use_c);
  auto& row = main ? dragging.main : dragging.child;
  dragging.Check(IsMainThread() && row.window && row.window->GetNativeObject(),
                 "native drag entry runs inside owning ArkTS touch callback");
  ++row.downs;
  if (use_c)
    native_window_start_dragging(row.alias);
  else if (row.window)
    row.window->StartDragging();
  return ohos::String(env, dragging.State());
}
}  // namespace nativeapi::tests
