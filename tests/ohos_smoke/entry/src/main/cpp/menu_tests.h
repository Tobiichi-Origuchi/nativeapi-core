#pragma once
#include "../../../../../../src/capi/menu_c.h"
#include "../../../../../../src/capi/positioning_strategy_c.h"
#include "../../../../../../src/menu.h"
#include "../../../../../../src/platform/ohos/ohos_menus.h"
namespace nativeapi::tests {
struct MenuTests : std::enable_shared_from_this<MenuTests> {
  napi_env env{};
  napi_deferred deferred{};
  uv_timer_t timer{};
  std::shared_ptr<Menu> root, submenu, doomed;
  std::shared_ptr<MenuItem> action, checkbox, disabled, more, nested, radio_a, radio_b;
  int passed = 0, failed = 0, ticks = 0, phase = 0, opened = 0, closed = 0, clicks = 0,
      disabled_clicks = 0, sub_open = 0, sub_close = 0, child_open = 0, child_close = 0,
      doomed_close = 0, pending_events = 0;
  native_menu_t c_menu = NATIVE_INVALID_MENU;
  native_menu_item_t c_item = NATIVE_INVALID_MENU_ITEM;
  int c_open = 0, c_close = 0, c_click = 0;
  Point anchor{};
  void Check(bool ok, const char* text) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-menu", "%{public}s %{public}s",
                 ok ? "PASS" : "FAIL", text);
  }
  void Open() {
    Check(root->Open(PositioningStrategy::Absolute(anchor), Placement::BottomStart),
          "native Menu open submitted");
  }
  void Begin() {
    root = std::make_shared<Menu>();
    submenu = std::make_shared<Menu>();
    auto w = WindowManager::GetInstance().GetCurrent();
    auto bounds = w->GetContentBounds();
    anchor = {bounds.x + 30, bounds.y + 100};
    Check(root->GetId() != IdAllocator::kInvalidId && submenu->GetId() != root->GetId(),
          "menus have stable distinct identity");
    Check(Menu::IsBackendSupported(MenuBackend::Native) &&
              !Menu::IsBackendSupported(MenuBackend::WinUI3) &&
              !root->SetBackend(MenuBackend::WinUI3),
          "native-only backend selected explicitly");
    Check(!root->Open(PositioningStrategy::Absolute(anchor)), "empty menu open rejected");
    action = std::make_shared<MenuItem>("NativeAPI Action");
    checkbox = std::make_shared<MenuItem>("NativeAPI Toggle", MenuItemType::Checkbox);
    disabled = std::make_shared<MenuItem>("NativeAPI Disabled");
    disabled->SetEnabled(false);
    checkbox->SetState(MenuItemState::Mixed);
    checkbox->SetTooltip("NativeAPI tooltip");
    action->SetAccelerator(KeyboardAccelerator("S", ModifierKey::Ctrl));
    auto image = Image::FromBase64(FixturePng);
    action->SetIcon(image);
    Check(action->GetIcon() == image && action->GetAccelerator().key == "S" &&
              checkbox->GetTooltip() == "NativeAPI tooltip",
          "menu icon, accelerator and tooltip configuration retained");
    radio_a = std::make_shared<MenuItem>("NativeAPI Radio A", MenuItemType::Radio);
    radio_b = std::make_shared<MenuItem>("NativeAPI Radio B", MenuItemType::Radio);
    radio_a->SetRadioGroup(427);
    radio_b->SetRadioGroup(427);
    radio_a->SetState(MenuItemState::Checked);
    radio_b->SetState(MenuItemState::Checked);
    Check(radio_a->GetState() == MenuItemState::Unchecked &&
              radio_b->GetState() == MenuItemState::Checked,
          "radio group enforces mutual exclusion");
    radio_a->SetState(MenuItemState::Mixed);
    Check(radio_a->GetState() == MenuItemState::Unchecked, "radio mixed state rejected");
    more = std::make_shared<MenuItem>("NativeAPI More");
    nested = std::make_shared<MenuItem>("NativeAPI Nested Action");
    submenu->AddItem(nested);
    more->SetSubmenu(submenu);
    root->AddItem(action);
    root->AddItem(checkbox);
    root->AddSeparator();
    root->AddItem(disabled);
    root->AddItem(radio_a);
    root->AddItem(radio_b);
    root->AddItem(more);
    root->InsertItem(0, nullptr);
    root->AddItem(action);
    Check(root->GetItemCount() == 7 && root->GetItemAt(0) == action &&
              root->GetItemById(more->GetId()) == more && root->GetAllItems().size() == 7,
          "native menu collection insertion, enumeration and duplicate guards");
    Check(!root->RemoveItemAt(100) && !root->RemoveItemById(0) && !root->GetItemAt(100),
          "invalid menu lookup and removal fail safely");
    Check(!root->Open(PositioningStrategy::Absolute(anchor), static_cast<Placement>(999)) &&
              !root->Open(
                  PositioningStrategy::Absolute({std::numeric_limits<double>::quiet_NaN(), 0})),
          "invalid placement and non-finite anchor rejected");
    auto separator = root->GetItemAt(2);
    Check(separator->GetType() == MenuItemType::Separator && root->RemoveItem(separator),
          "remove by identity");
    root->InsertSeparator(2);
    nested->SetSubmenu(root);
    Check(!nested->GetSubmenu(), "submenu cycle rejected");
    Menu borrowed(root->GetNativeObject());
    MenuItem borrowed_item(action->GetNativeObject());
    Check(borrowed.GetId() == root->GetId() && borrowed_item.GetId() == action->GetId() &&
              !borrowed.SetBackend(MenuBackend::Native),
          "opaque native menu/item wrapping preserves identity");
    root->AddListener<MenuOpenedEvent>([this](const auto& e) {
      Check(e.GetMenuId() == root->GetId(), "opened event carries menu identity");
      ++opened;
    });
    root->AddListener<MenuClosedEvent>([this](const auto& e) {
      Check(e.GetMenuId() == root->GetId(), "closed event carries menu identity");
      ++closed;
    });
    checkbox->AddListener<MenuItemClickedEvent>([this](const auto& e) {
      Check(e.GetItemId() == checkbox->GetId(), "native click carries menu-item identity");
      ++clicks;
      checkbox->SetState(MenuItemState::Checked);
    });
    disabled->AddListener<MenuItemClickedEvent>([this](const auto&) { ++disabled_clicks; });
    nested->AddListener<MenuItemClickedEvent>([this](const auto&) { ++clicks; });
    more->AddListener<MenuItemSubmenuOpenedEvent>([this](const auto&) { ++sub_open; });
    more->AddListener<MenuItemSubmenuClosedEvent>([this](const auto&) { ++sub_close; });
    submenu->AddListener<MenuOpenedEvent>([this](const auto&) { ++child_open; });
    submenu->AddListener<MenuClosedEvent>([this](const auto&) { ++child_close; });
    Open();
    phase = 1;
  }
  void Tick() {
    if (++ticks > 450) {
      Check(false, "native menu scenario finishes before timeout");
      Finish();
      return;
    }
    if (phase == 1 && opened == 1) {
      Check(!root->Open(PositioningStrategy::Absolute(anchor)) &&
                !root->SetBackend(MenuBackend::Native),
            "open menu rejects duplicate presentation/backend switch");
      OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-menu", "NATIVEAPI_MENU_UI_READY toggle");
      phase = 2;
    } else if (phase == 2 && clicks == 1 && closed == 1) {
      Check(disabled_clicks == 0 && checkbox->GetState() == MenuItemState::Checked,
            "disabled click ignored and application updates checked state");
      Open();
      phase = 3;
      ticks = 0;
    } else if (phase == 3 && opened == 2 && ticks >= 4) {
      action->SetLabel("NativeAPI Updated");
      auto temporary = std::make_shared<MenuItem>("NativeAPI Must Be Removed");
      root->InsertItem(1, temporary);
      root->RemoveItemById(temporary->GetId());
      OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-menu", "NATIVEAPI_MENU_UI_READY submenu");
      phase = 4;
    } else if (phase == 4 && clicks == 2 && closed == 2) {
      Check(sub_open == 1 && sub_close == 1 && child_open == 1 && child_close == 1,
            "native submenu open/close events delivered once to menu and owning item");
      c_menu = native_menu_create();
      c_item = native_menu_item_create_with_label_and_type("NativeAPI C Menu",
                                                           NATIVE_MENU_ITEM_TYPE_NORMAL);
      native_menu_add_item(c_menu, c_item);
      native_menu_add_listener(
          c_menu,
          [](const native_menu_event_t* event, void* data) {
            auto* t = static_cast<MenuTests*>(data);
            if (event->type == NATIVE_MENU_EVENT_TYPE_OPENED)
              ++t->c_open;
            if (event->type == NATIVE_MENU_EVENT_TYPE_CLOSED)
              ++t->c_close;
          },
          this, nullptr);
      native_menu_item_add_listener(
          c_item,
          [](const native_menu_event_t* event, void* data) {
            if (event->type == NATIVE_MENU_EVENT_TYPE_ITEM_CLICKED)
              ++static_cast<MenuTests*>(data)->c_click;
          },
          this, nullptr);
      auto position = native_positioning_strategy_relative_with_rect_and_offset(
          {anchor.x + 20, anchor.y + 20, 100, 30}, {5, 7});
      Check(native_positioning_strategy_get_type(position) ==
                    NATIVE_POSITIONING_STRATEGY_TYPE_RELATIVE &&
                native_menu_open(c_menu, position, NATIVE_PLACEMENT_BOTTOM_START),
            "C ABI relative rectangle/offset opens actual ArkUI menu");
      native_positioning_strategy_free(position);
      phase = 5;
    } else if (phase == 5 && c_open == 1) {
      OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-menu", "NATIVEAPI_MENU_UI_READY c");
      phase = 6;
    } else if (phase == 6 && c_click == 1 && c_close == 1) {
      Check(c_open == 1 && c_click == 1 && c_close == 1,
            "C ABI menu lifecycle and item click exactly once");
      native_menu_item_free(c_item);
      c_item = 0;
      native_menu_free(c_menu);
      c_menu = 0;
      auto pending = std::make_shared<Menu>();
      pending->AddItem(std::make_shared<MenuItem>("NativeAPI Never Appears"));
      pending->AddListener<MenuEvent>([this](const auto&) { ++pending_events; });
      Check(pending->Open(PositioningStrategy::Absolute(anchor)) && pending->Close(),
            "pending native menu presentation cancels before appearance");
      Open();
      phase = 7;
    } else if (phase == 7 && opened == 3) {
      Check(!submenu->Close(), "hidden submenu close reports false");
      OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-menu", "NATIVEAPI_MENU_UI_READY submenu-close");
      phase = 12;
    } else if (phase == 12 && sub_open == 2) {
      Check(submenu->Close(), "visible submenu closes native popup hierarchy");
      phase = 13;
    } else if (phase == 13 && closed == 3 && sub_close == 2) {
      Check(child_open == 2 && child_close == 2,
            "submenu closure emits actual hierarchy lifecycle once");
      Open();
      phase = 14;
    } else if (phase == 14 && opened == 4) {
      Check(root->Close(), "programmatic close submitted");
      phase = 8;
    } else if (phase == 8 && closed == 4) {
      Check(!root->Close(), "already closed menu reports false");
      doomed = std::make_shared<Menu>();
      doomed->AddItem(std::make_shared<MenuItem>("NativeAPI Disposed Menu"));
      doomed->AddListener<MenuClosedEvent>([this](const auto&) { ++doomed_close; });
      doomed->AddListener<MenuOpenedEvent>([this](const auto&) {
        phase = 10;
        ticks = 0;
      });
      Check(doomed->Open(PositioningStrategy::Absolute(anchor)),
            "destruction case presents real menu");
      phase = 9;
    } else if (phase == 10 && ticks >= 3) {
      doomed.reset();
      phase = 11;
      ticks = 0;
    } else if (phase == 11 && ticks >= 5) {
      Check(pending_events == 0, "cancelled pending menu emits no synthetic lifecycle event");
      Check(doomed_close == 0, "disposed menu suppresses late lifecycle delivery");
      root->Clear();
      Check(root->GetItemCount() == 0, "menu clear releases collection");
      Finish();
    }
  }
  void Finish() {
    const auto report = std::string(failed ? "NATIVEAPI_MENU_FAIL" : "NATIVEAPI_MENU_PASS") + " (" +
                        std::to_string(passed) + " passed, " + std::to_string(failed) + " failed)";
    OH_LOG_Print(LOG_APP, failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-menu", "%{public}s",
                 report.c_str());
    napi_value result = ohos::String(env, report);
    napi_resolve_deferred(env, deferred, result);
    uv_timer_stop(&timer);
    uv_close(reinterpret_cast<uv_handle_t*>(&timer),
             [](auto* h) { delete static_cast<std::shared_ptr<MenuTests>*>(h->data); });
  }
  ~MenuTests() {
    if (c_menu)
      native_menu_free(c_menu);
    if (c_item)
      native_menu_item_free(c_item);
  }
};
inline napi_value AttachMenus(napi_env env, napi_callback_info info) {
  size_t count = 2;
  napi_value args[2], result;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  napi_get_boolean(env, count == 2 && ohos::AttachMenuHost(env, args[0], args[1]), &result);
  return result;
}
inline napi_value RunMenus(napi_env env, napi_callback_info) {
  auto t = std::make_shared<MenuTests>();
  t->env = env;
  napi_value promise;
  napi_create_promise(env, &t->deferred, &promise);
  uv_loop_t* loop;
  napi_get_uv_event_loop(env, &loop);
  uv_timer_init(loop, &t->timer);
  t->timer.data = new std::shared_ptr<MenuTests>(t);
  uv_timer_start(
      &t->timer,
      [](uv_timer_t* h) { (*static_cast<std::shared_ptr<MenuTests>*>(h->data))->Tick(); }, 200,
      200);
  t->Begin();
  return promise;
}
}  // namespace nativeapi::tests
