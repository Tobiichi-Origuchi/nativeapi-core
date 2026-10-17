#pragma once
#include "../../../../../../src/shortcut_manager.h"
#include "../../../../../../src/capi/shortcut_manager_c.h"
#include "../../../../../../src/platform/ohos/ohos_shortcut.h"
#include "../../../../../../src/platform/ohos/ohos_local_shortcuts.h"

namespace nativeapi::tests {
inline std::atomic<int> shortcut_sdk_events{0};
inline void ShortcutSdkReference(Input_Hotkey*) { ++shortcut_sdk_events; }
struct ShortcutJournal {
  int passed = 0, failed = 0, callbacks = 0, registered = 0, unregistered = 0, rejected = 0, activated = 0;
  bool last_focused = false, remove_on_activation = false;
  int released = 0;
  size_t listener = 0;
  std::shared_ptr<Shortcut> held;
  native_shortcut_t alias = 0;
  std::vector<ohos::Hotkey> references;
  void Check(bool ok, const char* text) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-shortcuts", "%{public}s %{public}s", ok ? "PASS" : "FAIL", text);
  }
};
inline ShortcutJournal shortcut_journal;
inline napi_value ShortcutHostAttach(napi_env env, napi_callback_info info) {
  napi_value target = nullptr, result;
  size_t count = 1;
  napi_get_cb_info(env, info, &count, &target, nullptr, nullptr);
  const auto token = count ? ohos::AttachLocalKeyHost(env, target) : 0;
  napi_create_int64(env, token, &result);
  return result;
}
inline napi_value ShortcutHostDetach(napi_env env, napi_callback_info info) {
  napi_value value = nullptr;
  size_t count = 1;
  int64_t token = 0;
  napi_get_cb_info(env, info, &count, &value, nullptr, nullptr);
  if (count && napi_get_value_int64(env, value, &token) == napi_ok)
    ohos::DetachLocalKeyHost(token);
  return nullptr;
}
inline napi_value ShortcutKey(napi_env env, napi_callback_info info) {
  napi_value args[5]{}, result;
  size_t count = 5;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  int64_t token = 0;
  int32_t key = -1, type = -1, device = -1;
  uint32_t modifiers = 0;
  const bool valid = count == 5 && napi_get_value_int64(env, args[0], &token) == napi_ok &&
      napi_get_value_int32(env, args[1], &key) == napi_ok &&
      napi_get_value_int32(env, args[2], &type) == napi_ok &&
      napi_get_value_uint32(env, args[3], &modifiers) == napi_ok &&
      napi_get_value_int32(env, args[4], &device) == napi_ok;
  const bool consumed = valid && ohos::DispatchLocalKey(token, key, type, modifiers, device);
  OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-shortcuts", "NATIVEAPI_LOCAL_KEY key=%{public}d type=%{public}d modifiers=%{public}u consumed=%{public}d", key, type, modifiers, int(consumed));
  napi_get_boolean(env, consumed, &result);
  return result;
}
inline napi_value ShortcutState(napi_env env, napi_callback_info info) {
  napi_value args[2]{};
  size_t count = 2;
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  auto command = count ? ohos::js::String(env, args[0]) : std::string();
  auto request = count > 1 ? ohos::js::String(env, args[1]) : std::string();
  auto& j = shortcut_journal;
  auto& manager = ShortcutManager::GetInstance();
  if (command == "setup") {
    j.Check(manager.IsSupported() && native_shortcut_manager_is_supported(), "public NDK hotkey service is supported through C++ and C");
    j.Check(manager.GetAll().empty(), "registration fixture starts with an empty manager");
    j.listener = manager.AddListener<ShortcutEvent>([](const auto& event) {
      auto& j = shortcut_journal;
      j.Check(IsMainThread(), "shortcut lifecycle events are delivered on UI");
      if (dynamic_cast<const ShortcutRegisteredEvent*>(&event)) ++j.registered;
      if (dynamic_cast<const ShortcutUnregisteredEvent*>(&event)) ++j.unregistered;
      if (dynamic_cast<const ShortcutRegistrationFailedEvent*>(&event)) ++j.rejected;
      if (dynamic_cast<const ShortcutActivatedEvent*>(&event)) {
        ++j.activated;
        if (j.remove_on_activation && j.held) {
          j.remove_on_activation = false;
          j.Check(ShortcutManager::GetInstance().Unregister(j.held->GetId()), "activation event may remove local registration before business callback");
        }
      }
    });
    for (auto pair : std::vector<std::pair<std::string, int32_t>>{
        {"Ctrl+A", KEYCODE_A}, {"Alt+0", KEYCODE_0}, {"Shift+F1", KEYCODE_F1},
        {"Control+F12", KEYCODE_F12}, {"Option+F13", KEYCODE_F13}, {"Ctrl+F24", KEYCODE_F24},
        {"Ctrl+Num0", KEYCODE_NUMPAD_0}, {"Alt+NumEnter", KEYCODE_NUMPAD_ENTER},
        {"Ctrl+PageDown", KEYCODE_PAGE_DOWN}, {"Alt+ForwardDelete", KEYCODE_FORWARD_DEL},
        {"Ctrl+;", KEYCODE_SEMICOLON}, {"Shift+Backquote", KEYCODE_GRAVE}}) {
      ohos::HotkeySpec spec, returned;
      const bool parsed = ohos::ParseHotkey(pair.first, spec);
      auto native = parsed ? spec.Create() : ohos::Hotkey{};
      j.Check(native && ohos::ReadHotkey(native.get(), returned) && returned.key == pair.second &&
                  returned.modifiers == spec.modifiers,
              "accelerator mapping survives actual SDK key/modifier object round trip");
    }
    for (const char* invalid : {"", "A", "Meta+A", "Fn+A", "CapsLock+A", "Ctrl+Ctrl+A", "Ctrl++A"})
      j.Check(!manager.IsAvailable(invalid) && !manager.Register(invalid, [] {}),
              "invalid or SDK-unrepresentable hotkeys are rejected without registration");
    ShortcutOptions local;
    local.accelerator = "Ctrl+Alt+F9";
    local.scope = ShortcutScope::Application;
    j.Check(!manager.Register(local), "application scope is not falsely implemented by a globally reserved chord");
  }
  if (command == "local-register" || command == "local-register-c" || command == "local-worker") {
    const auto register_local = [&] {
      ShortcutOptions options;
      options.accelerator = "Ctrl+Alt+F9";
      options.scope = ShortcutScope::Application;
      options.callback = [] {
        auto& j = shortcut_journal;
        ++j.callbacks;
        auto window = Application::GetInstance().GetPrimaryWindow();
        j.last_focused = window && window->IsFocused();
        j.Check(IsMainThread() && j.last_focused, "actual local callback runs on focused UI");
      };
      if (command == "local-register-c") {
        native_shortcut_options_t c{};
        char accelerator[] = "Ctrl+Alt+F9";
        c.accelerator = accelerator;
        c.scope = NATIVE_SHORTCUT_SCOPE_APPLICATION;
        c.enabled = true;
        c.callback_user_data = &j;
        c.callback_release_user_data = [](void* data) {
          auto* journal = static_cast<ShortcutJournal*>(data);
          ++journal->released;
        };
        c.callback = [](void* data) {
          shortcut_journal.Check(data == &shortcut_journal, "actual C local callback retains the registered user data");
          auto& j = shortcut_journal;
          ++j.callbacks;
          auto window = Application::GetInstance().GetPrimaryWindow();
          j.last_focused = window && window->IsFocused();
          j.Check(IsMainThread() && j.last_focused, "actual C local callback runs on focused UI");
        };
        j.alias = native_shortcut_manager_register_with_options(c);
        j.held = j.alias ? manager.Get(native_shortcut_get_id(j.alias)) : nullptr;
      } else {
        j.held = manager.Register(options);
        j.alias = j.held ? native_shortcut_manager_get_with_id(j.held->GetId()) : 0;
      }
    };
    if (command == "local-worker") { std::thread worker(register_local); worker.join(); }
    else register_local();
    j.Check(j.held && j.alias && j.held->GetScope() == ShortcutScope::Application,
            "local registration has shared C++ and C identity without global reservation");
    j.Check(manager.GetByScope(ShortcutScope::Application).size() == 1 && manager.GetByScope(ShortcutScope::Global).empty(),
            "local scope registry is separate from global scope");
    j.Check(!manager.Register("Control+Option+F9", [] {}), "global registration cannot duplicate an owned local chord");
    ShortcutOptions duplicate;
    duplicate.accelerator = "Alt+Control+F9";
    duplicate.scope = ShortcutScope::Application;
    j.Check(!manager.Register(duplicate), "local aliases and modifier order cannot duplicate a chord");
  }
  if (command == "local-event-remove") j.remove_on_activation = true;
  if (command == "local-release") {
    j.Check(j.held && manager.Unregister(j.held->GetId()), "local SDK host registration can be removed");
    j.Check(j.held && !j.held->GetCallback() && manager.GetAll().empty(), "retained local callback is invalidated after removal");
    native_shortcut_free(j.alias); j.alias = 0;
  }
  if (command == "register") {
    j.Check(manager.IsAvailable("Ctrl+Alt+F9"), "unused chord is available before native registration");
    j.held = manager.Register("Ctrl+Alt+F9", [] {
      auto& j = shortcut_journal;
      ++j.callbacks;
      auto win = Application::GetInstance().GetPrimaryWindow();
      j.last_focused = win && win->IsFocused();
      j.Check(IsMainThread(), "real native hotkey callback is delivered on UI");
    });
    j.Check(j.held && j.held->GetScope() == ShortcutScope::Global, "C++ shortcut really registers its native global chord");
    if (j.held) {
      j.alias = native_shortcut_manager_get_with_id(j.held->GetId());
      j.Check(j.alias && native_shortcut_get_id(j.alias) == j.held->GetId() && manager.Get(j.held->GetId()) == j.held,
              "C++ and C registry aliases preserve one shortcut identity");
      j.held->SetDescription("SDK global chord 中文");
      char* description = native_shortcut_get_description(j.alias);
      j.Check(description && j.held->GetDescription() == description, "C++ and C expose copied shortcut metadata");
      free_c_str(description);
      j.Check(!manager.IsAvailable("Control+Option+F9") && !manager.Register("Alt+Ctrl+F9", [] {}),
              "canonical modifier aliases and order cannot duplicate a physical chord");
      const auto size = manager.GetAll().size();
      j.Check(size == 1 && manager.GetByScope(ShortcutScope::Global).size() == 1 && manager.GetByScope(ShortcutScope::Application).empty(),
              "registration snapshots and scope filtering reflect real successful registrations");
      ohos::HotkeySpec spec;
      ohos::ParseHotkey("Ctrl+Alt+F9", spec);
      auto variants = ohos::HotkeyVariants(spec);
      j.Check(variants.size() == 4, "both sides of two modifiers produce four SDK registrations");
    }
  }
  if (command == "input-reference" && j.references.empty()) {
    ohos::HotkeySpec spec;
    ohos::ParseHotkey("Ctrl+Alt+F9", spec);
    j.references = ohos::HotkeyVariants(spec);
    for (const auto& native : j.references)
      j.Check(OH_Input_AddHotkeyMonitor(native.get(), ShortcutSdkReference) == INPUT_SUCCESS,
              "independent direct SDK hotkey reference observes the physical modifier variant");
  }
  if (command == "worker") {
    std::thread worker([&] {
      auto alias = native_shortcut_manager_register_with_accelerator_and_callback("Ctrl+Alt+F10", [] (void*) {}, nullptr, nullptr);
      j.Check(alias && native_shortcut_get_scope(alias) == NATIVE_SHORTCUT_SCOPE_GLOBAL,
              "worker C ABI registration uses the actual public NDK");
      if (alias) {
        native_shortcut_set_enabled(alias, false);
        j.Check(!native_shortcut_is_enabled(alias), "worker C alias updates the shared shortcut state");
        j.Check(native_shortcut_manager_unregister_with_id(native_shortcut_get_id(alias)), "worker C ABI unregisters the actual hotkey");
        native_shortcut_free(alias);
      }
    });
    worker.join();
  }
  if (command == "disable-manager" || command == "enable-manager") {
    manager.SetEnabled(command == "enable-manager");
    j.Check(manager.IsEnabled() == (command == "enable-manager"), "manager enabled state updates");
  }
  if (command == "disable-shortcut" || command == "enable-shortcut") {
    native_shortcut_set_enabled(j.alias, command == "enable-shortcut");
    j.Check(j.held && j.held->IsEnabled() == (command == "enable-shortcut"), "C alias controls the held shortcut enabled state");
  }
  if (command == "self-remove" && j.held) {
    const auto id = j.held->GetId();
    j.held->SetCallback([id] {
      auto& j = shortcut_journal;
      ++j.callbacks;
      j.Check(IsMainThread() && ShortcutManager::GetInstance().Unregister(id), "actual activation callback may unregister itself");
    });
  }
  if (command == "release") {
    j.Check(j.held && manager.Unregister(j.held->GetId()), "native global registration can be released by ID");
    j.Check(manager.GetAll().empty() && manager.IsAvailable("Alt+Ctrl+F9"), "all modifier variants are reusable after unregister");
    if (j.held) {
      const int before = j.callbacks;
      j.held->Invoke();
      j.Check(j.callbacks == before && !j.held->GetCallback(), "retained shortcut handles cannot invoke an ended registration");
    }
    native_shortcut_free(j.alias);
    j.alias = 0;
  }
  if (command == "conflict") {
    const auto before = manager.GetAll().size();
    j.Check(!manager.IsAvailable("Ctrl+Alt+F9"), "availability probes detect an independent SDK application owner");
    j.Check(!manager.Register("Ctrl+Alt+F9", [] {}), "independent SDK application occupancy rejects native registration");
    j.Check(manager.GetAll().size() == before, "failed native registration adds no public registry identity");
  }
  if (command == "clear" || command == "finish") {
    for (const auto& native : j.references)
      j.Check(OH_Input_RemoveHotkeyMonitor(native.get(), ShortcutSdkReference) == INPUT_SUCCESS,
              "independent SDK input reference removed");
    j.references.clear();
    const auto expected = manager.GetAll().size();
    j.Check(manager.UnregisterAll() == static_cast<int>(expected), "all remaining native registrations are removed");
    if (command == "finish" && j.listener) {
      j.Check(manager.RemoveListener(j.listener), "shortcut lifecycle observer removed");
      j.listener = 0;
    }
    native_shortcut_free(j.alias); j.alias = 0;
    j.Check(manager.GetAll().empty(), "finished fixture owns no global key reservation");
  }
  std::ostringstream out;
  out << "{\"released\":" << j.released << ",\"passed\":" << j.passed << ",\"failed\":" << j.failed << ",\"callbacks\":" << j.callbacks
      << ",\"sdkCallbacks\":" << shortcut_sdk_events.load() << ",\"registered\":" << j.registered << ",\"unregistered\":" << j.unregistered
      << ",\"lastFocused\":" << (j.last_focused ? "true" : "false") << ",\"rejected\":" << j.rejected << ",\"activated\":" << j.activated << ",\"count\":" << manager.GetAll().size() << '}';
  OH_LOG_Print(LOG_APP, LOG_INFO, 1, "nativeapi-shortcuts", "NATIVEAPI_SHORTCUT_REPLY %{public}s %{public}s %{public}s", request.c_str(), command.c_str(), out.str().c_str());
  napi_value result;
  napi_create_string_utf8(env, out.str().c_str(), NAPI_AUTO_LENGTH, &result);
  return result;
}
}  // namespace nativeapi::tests
