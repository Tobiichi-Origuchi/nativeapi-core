#pragma once
#include "../../../../../../src/capi/view_c.h"
#include "../../../../../../src/platform/ohos/ohos_arkui.h"
#include "../../../../../../src/platform/ohos/ohos_views.h"
#include "../../../../../../src/view_impl.h"
namespace nativeapi::tests {
struct ViewTests : std::enable_shared_from_this<ViewTests> {
  std::shared_ptr<View> root, row, nest;
  std::shared_ptr<Label> label, hidden;
  std::shared_ptr<Button> next, disabled;
  std::shared_ptr<TextField> field, secure;
  std::shared_ptr<ImageView> picture;
  native_button_t c_button = NATIVE_INVALID_BUTTON;
  uv_timer_t timer{};
  int passed = 0, failed = 0, ticks = 0, phase = 0, clicks = 0, c_clicks = 0, disabled_clicks = 0,
      changes = 0;
  size_t c_listener = 0;
  bool resized = false;
  Size original_window{}, original_root{};
  void Check(bool ok, const char* text) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-view", "%{public}s %{public}s",
                 ok ? "PASS" : "FAIL", text);
  }
  static bool Near(double a, double b) { return std::abs(a - b) < 1.5; }
  static bool Actual(const std::shared_ptr<View>& view) {
    ArkUI_IntSize size{};
    auto node = static_cast<ArkUI_NodeHandle>(view->GetNativeObject());
    auto w = view->GetWindow();
    auto state = w ? ohos::FindWindowState(w->GetNativeObject()) : nullptr;
    auto scale = ohos::WindowScale(state);
    auto frame = view->GetFrame();
    return node && OH_ArkUI_NodeUtils_GetLayoutSize(node, &size) == 0 &&
           Near(size.width / scale, frame.width) && Near(size.height / scale, frame.height);
  }
  void Begin() {
    auto window = WindowManager::GetInstance().GetCurrent();
    root = window ? window->GetContentView() : nullptr;
    Check(View::IsSupported() && View::IsBackendSupported(ViewBackend::Native),
          "native view module available");
    Check(!View::IsBackendSupported(ViewBackend::WinUI3) &&
              !View::SetDefaultBackend(ViewBackend::WinUI3),
          "unsupported backend rejected");
    Check(root && root == window->GetContentView() && root->GetWindow() == window,
          "mounted content root stable and associated with actual window");
    if (!root) {
      Finish();
      return;
    }
    root->ClearSubviews();
    root->SetLayout(ViewLayout::Column);
    root->SetPadding({16, 16, 16, 16});
    root->SetSpacing(10);
    label = std::make_shared<Label>("NativeAPI Views Ready 中文 🙂");
    label->SetPreferredSize({0, 38});
    label->SetFontSize(20);
    label->SetTextColor(Color::Blue);
    label->SetTextAlignment(TextAlignment::Center);
    Check(label->GetText() == "NativeAPI Views Ready 中文 🙂" && label->GetFontSize() == 20 &&
              label->GetTextColor().b == 255 && label->GetTextAlignment() == TextAlignment::Center,
          "native label text and appearance");
    const auto intrinsic = label->GetIntrinsicSize();
    Check(intrinsic.width > 0 && intrinsic.height > 0, "text engine measures intrinsic size");
    row = std::make_shared<View>();
    row->SetLayout(ViewLayout::Row);
    row->SetSpacing(8);
    row->SetPreferredSize({0, 48});
    next = std::make_shared<Button>("NativeAPI View Step 1");
    next->SetFlex(1);
    disabled = std::make_shared<Button>("NativeAPI Disabled");
    disabled->SetFlex(1);
    disabled->SetEnabled(false);
    disabled->AddListener<ButtonClickedEvent>([this](const auto&) { ++disabled_clicks; });
    Check(!disabled->IsEnabled(), "native disabled state applied");
    next->AddListener<ButtonClickedEvent>([this](const auto& event) {
      Check(event.GetViewId() == next->GetId(), "C++ click carries correct view identity");
      ++clicks;
    });
    row->AddSubview(next);
    row->AddSubview(disabled);
    hidden = std::make_shared<Label>("NativeAPI Must Be Hidden");
    hidden->SetVisible(false);
    row->AddSubview(hidden);
    Check(row->GetSubviewCount() == 3 && hidden->GetParent() == row && !hidden->IsVisible(),
          "shared tree and hidden state");
    field = std::make_shared<TextField>("NativeAPI prefilled 中文 🙂");
    field->SetPlaceholder("NativeAPI hint");
    field->SetPreferredSize({0, 48});
    field->SetFontSize(18);
    field->SetTextColor(Color::Red);
    field->SetTextAlignment(TextAlignment::Center);
    field->AddListener<TextFieldChangedEvent>([this](const auto&) { ++changes; });
    field->SetText("NativeAPI prefilled 中文 🙂");
    field->SetEditable(false);
    Check(!field->IsEditable() && field->GetText() == "NativeAPI prefilled 中文 🙂" &&
              field->GetPlaceholder() == "NativeAPI hint",
          "read-only prefilled field retains text and hint");
    auto identity = field->GetId();
    auto before = field->GetNativeObject();
    field->SetMultiline(true);
    Check(field->IsMultiline() && field->GetId() == identity &&
              field->GetNativeObject() != before &&
              field->GetText() == "NativeAPI prefilled 中文 🙂",
          "multiline replacement preserves identity and text");
    field->SetMultiline(false);
    Check(!field->IsMultiline() && !field->IsEditable() && field->GetFontSize() == 18 &&
              field->GetTextAlignment() == TextAlignment::Center,
          "single-line replacement preserves properties");
    secure = std::make_shared<TextField>("NativeAPI secret");
    secure->SetSecure(true);
    secure->SetPreferredSize({0, 48});
    Check(secure->IsSecure(), "native secure input type applied");
    secure->SetMultiline(true);
    Check(secure->IsSecure() && !secure->IsMultiline(),
          "unsupported secure multiline combination leaves state intact");
    picture = std::make_shared<ImageView>();
    picture->SetPreferredSize({96, 64});
    picture->SetAlignment(ViewAlignment::Center);
    auto image = Image::FromBase64(FixturePng);
    picture->SetImage(image);
    Check(image && picture->GetImage() == image &&
              picture->GetIntrinsicSize().width == image->GetSize().width,
          "ImageView retains actual PixelMap and natural dimensions");
    root->AddSubview(label);
    root->AddSubview(row);
    root->AddSubview(field);
    root->AddSubview(secure);
    root->AddSubview(picture);
    nest = std::make_shared<View>();
    nest->SetPreferredSize({0, 48});
    root->AddSubview(nest);
    c_button = native_button_create("NativeAPI C Step");
    native_view_set_enabled(c_button, false);
    native_view_set_frame(c_button, {0, 0, 240, 44});
    c_listener = native_view_add_listener(
        c_button,
        [](const native_view_event_t* event, void* data) {
          auto* self = static_cast<ViewTests*>(data);
          if (event->type == NATIVE_VIEW_EVENT_TYPE_BUTTON_CLICKED)
            ++self->c_clicks;
        },
        this, nullptr);
    auto c_view = HandleTable::GetInstance().Resolve<View>(c_button);
    nest->AddSubview(c_view);
    Check(c_view && c_listener && c_view->GetParent() == nest,
          "C ABI control participates in native tree");
    auto detached_parent = std::make_shared<View>();
    auto survivor = std::make_shared<Label>("retained");
    detached_parent->AddSubview(survivor);
    detached_parent.reset();
    survivor->SetText("still usable");
    Check(!survivor->GetParent() && survivor->GetText() == "still usable",
          "retained child survives parent destruction");
    auto absolute = std::make_shared<View>();
    absolute->SetFrame({13, 17, 80, 20});
    Check(Near(absolute->GetFrame().x, 13) && Near(absolute->GetFrame().y, 17) &&
              Near(absolute->GetFrame().width, 80),
          "detached absolute geometry remains logical");
    label->AddSubview(root);
    Check(!root->GetParent(), "ancestor cycle rejected");
    next->Focus();
    phase = 1;
  }
  void Tick() {
    if (++ticks > 450) {
      Check(false, "guarded UI interaction completed before timeout");
      Finish();
      return;
    }
    if (phase == 10 && ticks >= 5) {
      auto bounds = root->GetFrame();
      Check(Near(bounds.width, original_root.width - 100) &&
                Near(bounds.height, original_root.height - 70) && Actual(row),
            "content root and child layout follow actual desktop window resize");
      root->GetWindow()->SetSize(original_window, false);
      phase = 11;
      ticks = 0;
      return;
    }
    if (phase == 11 && ticks >= 5) {
      auto bounds = root->GetFrame();
      Check(Near(bounds.width, original_root.width) && Near(bounds.height, original_root.height) &&
                Actual(row),
            "native root and child geometry restore with desktop window");
      next->Focus();
      phase = 1;
      ticks = 0;
      return;
    }
    if (phase == 1 && ticks >= 5) {
      auto w = root->GetWindow();
      if (!resized && ohos::IsDesktopMainWindow(ohos::FindWindowState(w->GetNativeObject()))) {
        resized = true;
        original_window = w->GetSize();
        auto bounds = root->GetFrame();
        original_root = {bounds.width, bounds.height};
        w->SetSize({original_window.width - 100, original_window.height - 70}, false);
        phase = 10;
        ticks = 0;
        return;
      }
      Check(Actual(root) && Actual(label) && Actual(row) && Actual(next) && Actual(field),
            "native measured sizes agree with shared layout");
      auto r = row->GetFrame(), a = next->GetFrame(), b = disabled->GetFrame();
      Check(a.width > next->GetIntrinsicSize().width &&
                b.width > disabled->GetIntrinsicSize().width && Near(b.x, a.width + 8) &&
                Near(a.width + b.width + 8, r.width),
            "row flex and spacing reach native geometry");
      Check(changes == 0, "programmatic text and node replacements emit no changed event");
      Check(next->IsFocused(), "native button accepts explicit focus");
      next->Blur();
      OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-view", "NATIVEAPI_VIEW_UI_READY first");
      phase = 2;
    } else if (phase == 2 && clicks == 1) {
      Check(disabled_clicks == 0, "disabled native button ignored guarded click");
      const auto id = label->GetId();
      row->AddSubview(label);
      label->SetFlex(1);
      label->SetText("NativeAPI Reparented 中文");
      Check(label->GetParent() == row && label->GetId() == id &&
                label->GetWindow() == root->GetWindow(),
            "reparenting preserves identity and owning window");
      picture->SetVisible(false);
      field->SetMultiline(true);
      field->SetText("NativeAPI multiline\nsecond line");
      field->SetPreferredSize({0, 80});
      next->SetText("NativeAPI View Step 2");
      phase = 3;
      ticks = 0;
    } else if (phase == 3 && ticks >= 4) {
      Check(field->IsMultiline() && Actual(field) &&
                field->GetText() == "NativeAPI multiline\nsecond line" && changes == 0,
            "attached multiline swap renders and remains silent");
      OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-view", "NATIVEAPI_VIEW_UI_READY second");
      phase = 4;
    } else if (phase == 4 && clicks == 2) {
      next->SetVisible(false);
      native_view_set_enabled(c_button, true);
      OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-view", "NATIVEAPI_VIEW_UI_READY c");
      phase = 5;
    } else if (phase == 5 && c_clicks == 1) {
      Check(clicks == 2 && c_clicks == 1 && disabled_clicks == 0,
            "C++ and C ABI native click delivery exactly once");
      native_view_remove_listener(c_button, c_listener);
      c_listener = 0;
      nest->ClearSubviews();
      native_button_free(c_button);
      c_button = NATIVE_INVALID_BUTTON;
      Check(nest->GetSubviewCount() == 0, "C ABI control removal and release");
      root->ClearSubviews();
      label->SetText("NativeAPI Views PASS");
      root->AddSubview(label);
      Check(!field->GetParent() && field->GetText() == "NativeAPI multiline\nsecond line",
            "detached retained field remains usable");
      picture->SetImage(nullptr);
      Check(!picture->GetImage(), "ImageView clears native source");
      Finish();
    }
  }
  void Finish() {
    OH_LOG_Print(LOG_APP, failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-view",
                 "NATIVEAPI_VIEW_%{public}s (%{public}d passed, %{public}d failed)",
                 failed ? "FAIL" : "PASS", passed, failed);
    uv_timer_stop(&timer);
    uv_close(reinterpret_cast<uv_handle_t*>(&timer),
             [](auto* handle) { delete static_cast<std::shared_ptr<ViewTests>*>(handle->data); });
  }
  ~ViewTests() {
    if (c_button) {
      if (c_listener)
        native_view_remove_listener(c_button, c_listener);
      native_button_free(c_button);
    }
  }
};
inline napi_value AttachViews(napi_env env, napi_callback_info info) {
  size_t count = 2;
  napi_value args[2], result;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  bool ok = count == 2 && ohos::AttachViewContent(env, args[0], args[1]);
  if (ok) {
    auto w = WindowManager::GetInstance().GetCurrent();
    auto root = w->GetContentView();
    auto state = ohos::FindWindowState(w->GetNativeObject());
    ohos::DetachViewContent(state);
    ok = ohos::AttachViewContent(env, args[0], args[1]) && w->GetContentView() == root &&
         ohos::AttachViewContent(env, args[0], args[1]);
  }
  napi_get_boolean(env, ok, &result);
  return result;
}
inline napi_value DetachViews(napi_env env, napi_callback_info) {
  auto w = WindowManager::GetInstance().GetCurrent();
  if (w)
    ohos::DetachViewContent(ohos::FindWindowState(w->GetNativeObject()));
  napi_value result;
  napi_get_undefined(env, &result);
  return result;
}
inline napi_value RunViews(napi_env env, napi_callback_info) {
  auto tests = std::make_shared<ViewTests>();
  uv_loop_t* loop;
  napi_get_uv_event_loop(env, &loop);
  uv_timer_init(loop, &tests->timer);
  tests->timer.data = new std::shared_ptr<ViewTests>(tests);
  uv_timer_start(
      &tests->timer,
      [](uv_timer_t* t) { (*static_cast<std::shared_ptr<ViewTests>*>(t->data))->Tick(); }, 200,
      200);
  tests->Begin();
  napi_value result;
  napi_get_undefined(env, &result);
  return result;
}
}  // namespace nativeapi::tests
