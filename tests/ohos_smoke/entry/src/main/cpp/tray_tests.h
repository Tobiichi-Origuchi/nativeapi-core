#pragma once
#include "../../../../../../src/capi/tray_icon_c.h"
#include "../../../../../../src/capi/tray_manager_c.h"
#include "../../../../../../src/tray_icon.h"
#include "../../../../../../src/tray_manager.h"
namespace nativeapi::tests {
inline constexpr const char* TrayMagenta =
    "iVBORw0KGgoAAAANSUhEUgAAABgAAAAYCAYAAADgdz34AAAAJUlEQVR4nGP4z/D/"
    "Py0xw6gFoxaMWjBqwagFoxaMWjBqwdCwAAAD1bmbJ8HB9wAAAABJRU5ErkJggg==";
inline constexpr const char* TrayCyan =
    "iVBORw0KGgoAAAANSUhEUgAAABgAAAAYCAYAAADgdz34AAAAJElEQVR4nGNg+P//"
    "P03xqAWjFoxaMGrBqAWjFoxaMGrB0LAAAMXouZvMgZVyAAAAAElFTkSuQmCC";
struct TrayJournal {
  std::shared_ptr<TrayIcon> tray, second;
  std::shared_ptr<Image> original, replacement;
  std::shared_ptr<Menu> menu, sub;
  std::shared_ptr<MenuItem> action, nested;
  native_tray_icon_t c_alias = 0;
  TrayIconId disposed_id = 0;
  int passed = 0, failed = 0, clicks = 0, c_clicks = 0, menu_clicks = 0;
  void Check(bool ok, const char* text) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-tray", "%{public}s %{public}s",
                 ok ? "PASS" : "FAIL", text);
  }
  void Setup() {
    auto& manager = TrayManager::GetInstance();
    Check(manager.IsSupported() == native_tray_manager_is_supported(),
          "C and C++ capability agree");
    tray = std::make_shared<TrayIcon>("nativeapi.tray.fixture");
    second = std::make_shared<TrayIcon>();
    original = Image::FromBase64(TrayMagenta);
    replacement = Image::FromBase64(TrayCyan);
    Check(tray->GetId() != IdAllocator::kInvalidId && second->GetId() != tray->GetId(),
          "stable distinct tray IDs");
    Check(tray->GetIdentifier() == "nativeapi.tray.fixture" && !tray->IsVisible(),
          "identifier and initial hidden state");
    TrayIcon invalid(reinterpret_cast<void*>(uintptr_t{123}));
    Check(invalid.GetId() == IdAllocator::kInvalidId && !invalid.GetNativeObject() &&
              !invalid.SetVisible(true),
          "foreign native token rejected without dereference");
    TrayIcon bad(std::string("bad\0name", 8));
    Check(bad.GetId() == IdAllocator::kInvalidId, "embedded NUL identifier rejected");
    auto old_id = second->GetId();
    second.reset();
    Check(!manager.Get(old_id), "last wrapper removes manager registration");
    second = std::make_shared<TrayIcon>();
    Check(!manager.Get(0) && manager.Get(tray->GetId())->GetId() == tray->GetId(),
          "manager lookup uses stable identity");
    auto all = manager.GetAll();
    bool found = false;
    for (auto& t : all)
      if (t->GetId() == tray->GetId())
        found = true;
    Check(found, "manager enumerates constructed trays");
    tray->SetIcon(original);
    second->SetIcon(replacement);
    tray->SetTooltip("NativeAPI tray initial");
    Check(original && replacement && tray->GetIcon() == original, "native Image is retained");
    tray->SetIconSize({26, 26});
    tray->SetIconSize({-1, 4});
    tray->SetIconPosition(TrayIconPosition::Right);
    Check(tray->GetIconSize().width == 26 && tray->GetIconPosition() == TrayIconPosition::Right,
          "host-controlled size and position metadata retained; invalid size rejected");
    tray->SetTitle("NativeAPI tray fixture");
    tray->SetTitle(std::string("a\0b", 3));
    Check(tray->GetTitle() == "NativeAPI tray fixture",
          "title configuration retained and embedded NUL rejected");
    tray->SetContextMenuTrigger(ContextMenuTrigger::Clicked);
    Check(tray->GetContextMenuTrigger() == ContextMenuTrigger::None,
          "unavailable left-click menu trigger is rejected");
    menu = std::make_shared<Menu>();
    sub = std::make_shared<Menu>();
    action = std::make_shared<MenuItem>("NativeAPI Tray Action");
    nested = std::make_shared<MenuItem>("NativeAPI Tray Nested");
    action->AddListener<MenuItemClickedEvent>([this](const auto& e) {
      ++menu_clicks;
      Check(e.GetItemId() == action->GetId() && IsMainThread(),
            "real status menu click reaches native MenuItem");
    });
    nested->AddListener<MenuItemClickedEvent>([this](const auto&) { ++menu_clicks; });
    sub->AddItem(nested);
    auto more = std::make_shared<MenuItem>("NativeAPI Tray More");
    more->SetSubmenu(sub);
    menu->AddItem(action);
    menu->AddSeparator();
    menu->AddItem(more);
    tray->SetContextMenu(menu);
    tray->SetContextMenuTrigger(ContextMenuTrigger::RightClicked);
    Check(tray->GetContextMenu() == menu &&
              tray->GetContextMenuTrigger() == ContextMenuTrigger::RightClicked,
          "right-click menu configured");
    Check(!tray->OpenContextMenu() && !tray->CloseContextMenu(),
          "SDK has no programmatic tray menu presenter");
    c_alias = native_tray_manager_get(tray->GetId());
    Check(c_alias && native_tray_icon_get_id(c_alias) == tray->GetId(),
          "C manager alias has same native identity");
    native_tray_icon_add_listener(
        c_alias,
        [](const native_tray_icon_event_t* e, void* data) {
          auto& j = *static_cast<TrayJournal*>(data);
          if (e->type == NATIVE_TRAY_ICON_EVENT_TYPE_CLICKED) {
            ++j.c_clicks;
            j.Check(j.tray && e->data.clicked.tray_icon_id == j.tray->GetId(),
                    "C ABI observes actual tray click");
          }
        },
        this, nullptr);
    tray->AddListener<TrayIconClickedEvent>([this](const auto& e) {
      ++clicks;
      Check(tray && e.GetTrayIconId() == tray->GetId() && IsMainThread(),
            "C++ receives actual SDK left click on UI thread");
    });
    Check(!native_tray_icon_is_visible(0) && !native_tray_icon_set_visible(0, true),
          "invalid C handles fail safely");
    if (manager.IsSupported())
      Check(tray->SetVisible(true), "native status bar add submitted");
    else
      Check(!tray->SetVisible(true) && !tray->IsVisible(),
            "phone rejects unavailable desktop tray service");
  }
  std::string Run(const std::string& command) {
    if (command == "setup")
      Setup();
    else if (command == "shown") {
      Check(tray && tray->IsVisible() && native_tray_icon_is_visible(c_alias),
            "SDK completion confirms visible C and C++ state");
      Check(tray->SetVisible(true), "repeat show is idempotent after completion");
      Check(!second->SetVisible(true) && !second->IsVisible(),
            "second live status item rejected per application");
    } else if (command == "update") {
      tray->SetIcon(replacement);
    } else if (command == "updated") {
      Check(tray->GetIcon() == replacement && tray->IsVisible(),
            "SDK completion confirms visible icon replacement");
    } else if (command == "template") {
      tray->SetIconTemplate(true);
    } else if (command == "templated") {
      Check(tray->IsIconTemplate() && tray->GetIcon() == replacement,
            "template uses real theme-specific masks without modifying source");
    } else if (command == "restore") {
      tray->SetIconTemplate(false);
    } else if (command == "restored") {
      Check(!tray->IsIconTemplate() && tray->IsVisible(), "colored native icon restored");
    } else if (command == "tooltip") {
      tray->SetTooltip("NativeAPI tray updated");
    } else if (command == "tooltipped") {
      Check(tray->GetTooltip() == "NativeAPI tray updated",
            "SDK completion confirms live hover text update");
    } else if (command == "bad-tooltip") {
      tray->SetTooltip(std::string(129, 'x'));
    } else if (command == "bad-tooltipped") {
      Check(tray->GetTooltip() == "NativeAPI tray updated" && tray->IsVisible(),
            "actual SDK rejects overlong hover text without changing applied state");
    } else if (command == "burst") {
      tray->SetIcon(original);
      tray->SetIcon(replacement);
      tray->SetIconTemplate(true);
      tray->SetIconTemplate(false);
      tray->SetTooltip("NativeAPI tray burst");
      tray->SetTooltip("NativeAPI tray burst final");
    } else if (command == "burst-done") {
      Check(tray->GetIcon() == replacement && !tray->IsIconTemplate() &&
                tray->GetTooltip() == "NativeAPI tray burst final" && tray->IsVisible(),
            "pending SDK setters coalesce to the last icon, template and tooltip");
      Check(!original->ToBase64().empty() && !replacement->ToBase64().empty(),
            "source PixelMaps remain usable after repeated SDK replacement");
    } else if (command == "hide") {
      Check(native_tray_icon_set_visible(c_alias, false),
            "C ABI real status item removal submitted");
    } else if (command == "hidden") {
      Check(!tray->IsVisible() && !native_tray_icon_is_visible(c_alias),
            "SDK completion confirms hidden status item");
    } else if (command == "show") {
      Check(native_tray_icon_set_visible(c_alias, true), "C ABI re-registration submitted");
    } else if (command == "rename-menu") {
      action->SetLabel("NativeAPI Tray Updated Action");
    } else if (command == "dispose") {
      disposed_id = tray->GetId();
      tray->SetIcon(original);  // Destroy while a real SDK update is pending.
      native_tray_icon_free(c_alias);
      c_alias = 0;
      tray.reset();
      second.reset();
      Check(!TrayManager::GetInstance().Get(disposed_id),
            "last-owner destruction unregisters manager identity");
    } else if (command == "finish") {
      if (tray) {
        native_tray_icon_free(c_alias);
        c_alias = 0;
        tray.reset();
        second.reset();
      }
      Check(failed == 0, "tray suite has no failed assertions");
      OH_LOG_Print(LOG_APP, failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-tray",
                   "NATIVEAPI_TRAY_FINAL_%{public}s passed=%{public}d failed=%{public}d",
                   failed ? "FAIL" : "PASS", passed, failed);
    }
    std::ostringstream s;
    s << "{\"supported\":" << (TrayManager::GetInstance().IsSupported() ? "true" : "false")
      << ",\"visible\":" << (tray && tray->IsVisible() ? "true" : "false")
      << ",\"replaced\":" << (tray && tray->GetIcon() == replacement ? "true" : "false")
      << ",\"template\":" << (tray && tray->IsIconTemplate() ? "true" : "false") << ",\"tooltip\":"
      << (tray && tray->GetTooltip() == "NativeAPI tray updated" ? "true" : "false")
      << ",\"burst\":"
      << (tray && tray->GetIcon() == replacement && !tray->IsIconTemplate() &&
                  tray->GetTooltip() == "NativeAPI tray burst final"
              ? "true"
              : "false")
      << ",\"workerTooltip\":"
      << (tray && tray->GetTooltip() == "NativeAPI worker tray" ? "true" : "false")
      << ",\"passed\":" << passed << ",\"failed\":" << failed << ",\"clicks\":" << clicks
      << ",\"cClicks\":" << c_clicks << ",\"menuClicks\":" << menu_clicks << "}";
    return s.str();
  }
};
inline TrayJournal tray_journal;
inline napi_value RunTrayWorker(napi_env env, napi_callback_info) {
  struct Work {
    napi_async_work work{};
    napi_deferred deferred{};
    TrayIconId id{};
    bool supported = false, visible = false, ok = false;
  };
  auto* job = new Work;
  job->id = tray_journal.tray->GetId();
  job->supported = TrayManager::GetInstance().IsSupported();
  job->visible = tray_journal.tray->IsVisible();
  napi_value promise, name;
  napi_create_promise(env, &job->deferred, &promise);
  napi_create_string_utf8(env, "nativeapi.tray.worker", NAPI_AUTO_LENGTH, &name);
  auto status = napi_create_async_work(
      env, nullptr, name,
      [](napi_env, void* data) {
        auto* job = static_cast<Work*>(data);
        auto alias = TrayManager::GetInstance().Get(job->id);
        auto c_alias = native_tray_manager_get(job->id);
        auto list = native_tray_manager_get_all();
        job->ok = !IsMainThread() && alias && alias->GetId() == job->id &&
                  TrayManager::GetInstance().IsSupported() == job->supported && c_alias &&
                  native_tray_icon_get_id(c_alias) == job->id &&
                  native_tray_icon_is_visible(c_alias) == job->visible && list.count >= 2;
        native_tray_icon_set_tooltip(c_alias, "NativeAPI worker tray");
        native_tray_icon_list_free(&list);
        native_tray_icon_free(c_alias);
      },
      [](napi_env env, napi_status status, void* data) {
        std::unique_ptr<Work> job(static_cast<Work*>(data));
        const bool ok = status == napi_ok && job->ok;
        tray_journal.Check(
            ok, "worker C++ and C ABI capability, registry, visibility and tooltip submission");
        napi_value result;
        napi_get_boolean(env, ok, &result);
        napi_resolve_deferred(env, job->deferred, result);
        napi_delete_async_work(env, job->work);
      },
      job, &job->work);
  if (status != napi_ok || napi_queue_async_work(env, job->work) != napi_ok) {
    if (job->work)
      napi_delete_async_work(env, job->work);
    tray_journal.Check(false, "worker task submission");
    napi_value result;
    napi_get_boolean(env, false, &result);
    napi_resolve_deferred(env, job->deferred, result);
    delete job;
  }
  return promise;
}
inline napi_value RunTray(napi_env env, napi_callback_info info) {
  size_t count = 1;
  napi_value arg = nullptr;
  napi_get_cb_info(env, info, &count, &arg, nullptr, nullptr);
  const auto command = ohos::js::String(env, arg);
  const auto result = tray_journal.Run(command);
  OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-tray", "NATIVEAPI_TRAY_STEP %{public}s %{public}s",
               command.c_str(), result.c_str());
  return ohos::String(env, result);
}
}  // namespace nativeapi::tests
