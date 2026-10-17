#pragma once
#include "../../../../../../src/application.h"
#include "../../../../../../src/capi/application_c.h"
#include "../../../../../../src/platform/ohos/ohos_napi.h"
namespace nativeapi::tests {
struct IntegrationJournal {
  std::shared_ptr<Window> main, child;
  std::shared_ptr<TrayIcon> tray;
  std::string a, b, nonsquare;
  bool desktop = false;
  int passed = 0, failed = 0, activated = 0, deactivated = 0;
  int c_activated = 0, c_deactivated = 0;
  void Check(bool ok, const char* text) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-integration",
                 "%{public}s %{public}s", ok ? "PASS" : "FAIL", text);
  }
  std::string State(bool ok = true) {
    std::ostringstream s;
    s << "{\"ok\":" << (ok ? "true" : "false") << ",\"desktop\":" << (desktop ? "true" : "false")
      << ",\"visible\":" << (Application::GetInstance().IsVisible() ? "true" : "false")
      << ",\"main\":" << (main && main->IsVisible() ? "true" : "false")
      << ",\"child\":" << (child && child->IsVisible() ? "true" : "false")
      << ",\"ready\":" << (main && (!desktop || child) ? "true" : "false")
      << ",\"tray\":" << (tray && tray->IsVisible() ? "true" : "false")
      << ",\"activated\":" << activated << ",\"deactivated\":" << deactivated
      << ",\"passed\":" << passed << ",\"failed\":" << failed << "}";
    return s.str();
  }
  void Setup() {
    auto& app = Application::GetInstance();
    desktop = std::string(OH_GetDeviceType()) == "2in1";
    main = app.GetPrimaryWindow();
    Check(main && main->GetNativeObject() && app.IsRunning(),
          "owning application and native main window exist");
    std::string cache;
    ohos::WithContext([&](napi_env env, napi_value context) {
      cache = ohos::js::StringProperty(env, context, "cacheDir");
    });
    a = cache + "/NativeAPI icon A 中文 #%.png";
    b = cache + "/NativeAPI icon B.png";
    nonsquare = cache + "/NativeAPI nonsquare.png";
    Check(Image::FromBase64(TrayMagenta)->SaveToFile(a) &&
              Image::FromBase64(TrayCyan)->SaveToFile(b) &&
              Image::FromBase64(FixturePng)->SaveToFile(nonsquare),
          "actual square and nonsquare codec files prepared");
    Check(!app.SetIcon("") && !app.SetIcon(cache + "/missing.png") && !app.SetIcon(nonsquare),
          "empty, missing and nonsquare icon requests fail");
    for (const std::string& label : {"-1", "+3", " 3", "3x", "word", "2147483648"})
      Check(!app.SetBadgeLabel(label), "unsupported or overflowing badge label rejected");
    Check(!app.SetBadgeLabel(std::string("3\0x", 3)), "embedded NUL badge label rejected");
    app.AddListener<ApplicationActivatedEvent>([this](const auto&) {
      ++activated;
      Check(Application::GetInstance().IsVisible(),
            "activation event exposes visible application state");
    });
    app.AddListener<ApplicationDeactivatedEvent>([this](const auto&) {
      ++deactivated;
      Check(!Application::GetInstance().IsVisible(),
            "deactivation event exposes hidden application state");
    });
    native_application_add_listener(
        [](const native_application_event_t* e, void* data) {
          auto& j = *static_cast<IntegrationJournal*>(data);
          if (e->type == NATIVE_APPLICATION_EVENT_TYPE_ACTIVATED)
            ++j.c_activated;
          if (e->type == NATIVE_APPLICATION_EVENT_TYPE_DEACTIVATED)
            ++j.c_deactivated;
        },
        this, nullptr);
    if (desktop) {
      tray = std::make_shared<TrayIcon>("nativeapi.application.integration");
      tray->SetIcon(Image::FromBase64(TrayMagenta));
      tray->SetTooltip("NativeAPI application integration");
      tray->AddListener<TrayIconClickedEvent>([this](const auto& e) {
        Check(e.GetTrayIconId() == tray->GetId() && Application::GetInstance().Show(),
              "actual status icon click requests application restoration");
      });
      Check(tray->SetVisible(true),
            "real status item registration submitted before application hiding");
      Check(ohos::CreateSubWindow("NativeAPI application integration child", "pages/Child",
                                  [this](auto w) {
                                    child = w;
                                    Check(bool(child),
                                          "native child window created for whole-Ability hiding");
                                    if (child) {
                                      child->SetBounds({30, 100, 240, 160});
                                      child->Show();
                                    }
                                  }),
            "native child creation submitted");
    }
  }
  bool Call(const std::string& command, bool use_c) {
    auto& app = Application::GetInstance();
    if (command == "badge9")
      return use_c ? native_application_set_badge_label("9") : app.SetBadgeLabel("9");
    if (command == "badge99")
      return use_c ? native_application_set_badge_label("99+") : app.SetBadgeLabel("99+");
    if (command == "clear")
      return use_c ? native_application_set_badge_label("") : app.SetBadgeLabel("");
    if (command == "icon-a")
      return use_c ? native_application_set_icon(a.c_str()) : app.SetIcon(a);
    if (command == "icon-b") {
      app.SetPrimaryWindow(nullptr);
      const bool ok = use_c ? native_application_set_icon(b.c_str()) : app.SetIcon(b);
      app.SetPrimaryWindow(main);
      return ok;
    }
    if (command == "window-show") {
      main->Show();
      return true;
    }
    if (command == "hide")
      return use_c ? native_application_hide() : app.Hide();
    if (command == "show")
      return use_c ? native_application_show() : app.Show();
    return false;
  }
  void Record(const std::string& command, bool ok, bool use_c, bool worker) {
    const bool supported =
        desktop || command == "badge9" || command == "badge99" || command == "clear";
    Check(ok == supported, "application action result agrees with actual supported platform");
    OH_LOG_Print(
        LOG_APP, LOG_INFO, 1, "nativeapi-integration",
        "NATIVEAPI_INTEGRATION_ACTION %{public}s c=%{public}d worker=%{public}d %{public}s",
        command.c_str(), use_c, worker, State(ok).c_str());
  }
  void Verify(const std::string& command) {
    if (command == "hidden") {
      Check(!Application::GetInstance().IsVisible() && main && !main->IsVisible() && child &&
                !child->IsVisible(),
            "real application hide removes main and child windows together");
      Check(deactivated == c_deactivated && deactivated > 0,
            "C++ and C ABI observe the same hidden transition");
    } else if (command == "shown") {
      Check(Application::GetInstance().IsVisible() && main && main->IsVisible() && child &&
                child->IsVisible(),
            "real application show restores main and child windows together");
      Check(activated == c_activated && activated == deactivated,
            "application visibility events balance after restoration");
      Check(Application::GetInstance().GetAllWindows().size() == 2 &&
                WindowManager::GetInstance().Get(main->GetId())->GetId() == main->GetId(),
            "hide/show preserves registered native window identities");
    } else if (command == "finish") {
      if (child)
        child->Close();
      child.reset();
      if (tray)
        tray->SetVisible(false);
      OH_LOG_Print(LOG_APP, failed ? LOG_ERROR : LOG_INFO, 1, "nativeapi-integration",
                   "NATIVEAPI_INTEGRATION_FINAL_%{public}s passed=%{public}d failed=%{public}d",
                   failed ? "FAIL" : "PASS", passed, failed);
    }
  }
};
inline IntegrationJournal integration;
inline napi_value SetupIntegrations(napi_env env, napi_callback_info) {
  integration.Setup();
  return ohos::String(env, integration.State());
}
inline napi_value IntegrationState(napi_env env, napi_callback_info info) {
  size_t count = 1;
  napi_value arg = nullptr;
  napi_get_cb_info(env, info, &count, &arg, nullptr, nullptr);
  if (count)
    integration.Verify(ohos::js::String(env, arg));
  return ohos::String(env, integration.State());
}
inline napi_value IntegrationAction(napi_env env, napi_callback_info info) {
  size_t count = 3;
  napi_value args[3]{};
  napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  struct Work {
    napi_async_work work{};
    napi_deferred deferred{};
    std::string command;
    bool c = false, worker = false, ok = false;
  };
  auto* job = new Work;
  job->command = ohos::js::String(env, args[0]);
  napi_get_value_bool(env, args[1], &job->c);
  napi_get_value_bool(env, args[2], &job->worker);
  napi_value promise, name;
  napi_create_promise(env, &job->deferred, &promise);
  auto complete = [](napi_env env, Work* job) {
    integration.Record(job->command, job->ok, job->c, job->worker);
    napi_resolve_deferred(env, job->deferred, ohos::String(env, integration.State(job->ok)));
  };
  if (!job->worker) {
    job->ok = integration.Call(job->command, job->c);
    complete(env, job);
    delete job;
    return promise;
  }
  napi_create_string_utf8(env, "nativeapi.application.integration", NAPI_AUTO_LENGTH, &name);
  napi_create_async_work(
      env, nullptr, name,
      [](napi_env, void* p) {
        auto* j = static_cast<Work*>(p);
        j->ok = integration.Call(j->command, j->c);
      },
      [](napi_env env, napi_status status, void* p) {
        std::unique_ptr<Work> j(static_cast<Work*>(p));
        if (status != napi_ok)
          j->ok = false;
        integration.Record(j->command, j->ok, j->c, j->worker);
        napi_resolve_deferred(env, j->deferred, ohos::String(env, integration.State(j->ok)));
        napi_delete_async_work(env, j->work);
      },
      job, &job->work);
  napi_queue_async_work(env, job->work);
  return promise;
}
}  // namespace nativeapi::tests
