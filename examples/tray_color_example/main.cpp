// A native C++ window and checkbox for TrayIcon::SetIconTemplate. The checkbox
// uses each desktop's native control; all tray behaviour stays in nativeapi.
#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <gtk/gtk.h>
#endif

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include "../../src/foundation/dispatcher.h"
#include "nativeapi.h"

using namespace nativeapi;

namespace {
constexpr const char* kBlueIcon =
    "iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAAAOElEQVR4nO3RwQkAMAgEQcu205ST1CB5"
    "nI9ZuKcwYJVW1eeO9nsHAAAAALAPoPgLAAAAAADiAAV6s6MvzK1rffgAAAAASUVORK5CYII=";

class SystemColourSwitch {
 public:
  SystemColourSwitch() {
#if defined(__APPLE__) || defined(_WIN32)
    button_ = std::make_shared<Button>("Follow system colours");
    view = button_;
#if defined(__APPLE__)
    [(__bridge NSButton*)button_->GetNativeObject() setButtonType:NSButtonTypeSwitch];
#else
    const auto hwnd = static_cast<HWND>(button_->GetNativeObject());
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~BS_TYPEMASK) | BS_AUTOCHECKBOX);
#endif
    listener_ = button_->AddListener<ButtonClickedEvent>([this](const auto&) { Changed(); });
#elif defined(__linux__)
    widget_ = gtk_check_button_new_with_label("Follow system colours");
    g_object_ref_sink(widget_);
    view = std::make_shared<View>(widget_);
    g_object_unref(widget_);  // The View wrapper retains it from here.
    gtk_widget_show(widget_);
    signal_ = g_signal_connect(widget_, "toggled", G_CALLBACK(+[](GtkToggleButton*, gpointer data) {
                                 static_cast<SystemColourSwitch*>(data)->Changed();
                               }),
                               this);
#endif
    view->SetPreferredSize(nativeapi::Size{0, 32});
    SetChecked(true);
  }

  ~SystemColourSwitch() {
#if defined(__APPLE__) || defined(_WIN32)
    button_->RemoveListener(listener_);
#elif defined(__linux__)
    g_signal_handler_disconnect(widget_, signal_);
#endif
  }

  bool IsChecked() const {
#if defined(__APPLE__)
    return [(__bridge NSButton*)button_->GetNativeObject() state] == NSControlStateValueOn;
#elif defined(_WIN32)
    return SendMessageW(static_cast<HWND>(button_->GetNativeObject()), BM_GETCHECK, 0, 0) ==
           BST_CHECKED;
#elif defined(__linux__)
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget_));
#endif
  }

  void SetChecked(bool value) {
    suppress_ = true;
#if defined(__APPLE__)
    [(__bridge NSButton*)button_->GetNativeObject()
        setState:value ? NSControlStateValueOn : NSControlStateValueOff];
#elif defined(_WIN32)
    SendMessageW(static_cast<HWND>(button_->GetNativeObject()), BM_SETCHECK,
                 value ? BST_CHECKED : BST_UNCHECKED, 0);
#elif defined(__linux__)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_), value);
#endif
    suppress_ = false;
  }

  // Exercises the control's real callback without moving the user's mouse.
  void Activate() {
#if defined(__APPLE__)
    [(__bridge NSButton*)button_->GetNativeObject() performClick:nil];
#elif defined(_WIN32)
    SendMessageW(static_cast<HWND>(button_->GetNativeObject()), BM_CLICK, 0, 0);
#elif defined(__linux__)
    gtk_button_clicked(GTK_BUTTON(widget_));
#endif
  }

  std::shared_ptr<View> view;
  std::function<void(bool)> on_changed;

 private:
  void Changed() {
    if (!suppress_ && on_changed)
      on_changed(IsChecked());
  }
  bool suppress_ = false;
#if defined(__APPLE__) || defined(_WIN32)
  std::shared_ptr<Button> button_;
  size_t listener_ = 0;
#elif defined(__linux__)
  GtkWidget* widget_ = nullptr;
  gulong signal_ = 0;
#endif
};
}  // namespace

int main(int argc, char** argv) {
  const bool self_test = argc > 1 && std::string(argv[1]) == "--self-test";
  if (!View::IsSupported() || !TrayManager::GetInstance().IsSupported()) {
    std::cerr << "This example requires desktop views and a system tray." << std::endl;
    return 1;
  }
  // Use native controls so the example's checkbox works without WinUI dependencies.
  View::SetDefaultBackend(ViewBackend::Native);
  auto& app = Application::GetInstance();
  auto window = std::make_shared<Window>();
  window->SetTitle("Tray Colours");
  window->SetContentSize(nativeapi::Size{460, 340});
  window->SetMinimumSize(nativeapi::Size{460, 340});
  window->Center();
  auto root = window->GetContentView();
  root->SetLayout(ViewLayout::Column);
  root->SetPadding(EdgeInsets::All(24));
  root->SetSpacing(14);

  auto heading = std::make_shared<Label>("Tray icon colours");
  heading->SetFontSize(24);
  auto description =
      std::make_shared<Label>("Look for the four-pane icon in your menu bar or taskbar.");
  description->SetPreferredSize(nativeapi::Size{0, 40});
  auto image = Image::FromBase64(kBlueIcon);
  if (!image)
    return 1;
  auto tray = std::make_shared<TrayIcon>("org.libnativeapi.tray-colours-example");
  tray->SetIcon(image);
  tray->SetIconTemplate(true);
  tray->SetTooltip("Tray Colours - follows system appearance");

  SystemColourSwitch toggle;
  auto status = std::make_shared<Label>();
  auto hint = std::make_shared<Label>(
      "When enabled, change your system's light/dark theme.\n"
      "When disabled, the icon keeps its original blue colour.");
  hint->SetPreferredSize(nativeapi::Size{0, 48});

  auto menu = std::make_shared<Menu>();
  auto automatic = std::make_shared<MenuItem>("Follow system colours", MenuItemType::Checkbox);
  auto apply = [&](bool enabled) {
    toggle.SetChecked(enabled);
    tray->SetIconTemplate(enabled);
    automatic->SetState(enabled ? MenuItemState::Checked : MenuItemState::Unchecked);
    status->SetText(enabled ? "Automatic: system tray foreground" : "Original: blue");
    tray->SetTooltip(enabled ? "Tray Colours - follows system appearance"
                             : "Tray Colours - original blue");
    std::cout << "[tray_colour] automatic=" << tray->IsIconTemplate()
              << " switch=" << toggle.IsChecked() << " status=" << status->GetText() << std::endl;
  };
  toggle.on_changed = apply;
  automatic->AddListener<MenuItemClickedEvent>(
      [&](const auto&) { apply(!tray->IsIconTemplate()); });
  menu->AddItem(automatic);
  auto show = std::make_shared<MenuItem>("Show window");
  show->AddListener<MenuItemClickedEvent>([window](const auto&) {
    window->Show();
    window->Focus();
  });
  menu->AddItem(show);
  menu->AddSeparator();
  auto quit = std::make_shared<MenuItem>("Quit");
  quit->AddListener<MenuItemClickedEvent>([&app](const auto&) { app.Quit(0); });
  menu->AddItem(quit);
  tray->SetContextMenu(menu);
  tray->SetContextMenuTrigger(ContextMenuTrigger::RightClicked);
  tray->AddListener<TrayIconClickedEvent>([window](const auto&) {
    window->Show();
    window->Focus();
  });

  auto close = std::make_shared<Button>("Quit");
  close->SetPreferredSize(nativeapi::Size{0, 32});
  close->AddListener<ButtonClickedEvent>([&app](const auto&) { app.Quit(0); });
  for (const auto& view :
       std::vector<std::shared_ptr<View>>{heading, description, toggle.view, status, hint, close}) {
    root->AddSubview(view);
  }
  apply(true);
  tray->SetVisible(true);
  auto closed = WindowManager::GetInstance().AddListener<WindowClosedEvent>([&](const auto& event) {
    if (event.GetWindowId() == window->GetId())
      app.Quit(0);
  });

  if (self_test) {
    const auto original = image->ToBase64();
    RunOnMainThread([&, original] {
      toggle.Activate();
      const bool off =
          !tray->IsIconTemplate() && !toggle.IsChecked() && status->GetText() == "Original: blue";
      toggle.Activate();
      const bool on = tray->IsIconTemplate() && toggle.IsChecked() &&
                      status->GetText() == "Automatic: system tray foreground";
      const bool unchanged = image->ToBase64() == original && tray->GetIcon() == image;
      std::cout << "[tray_colour] self-test off=" << off << " on=" << on << " source=" << unchanged
                << std::endl;
      app.Quit(off && on && unchanged ? 0 : 2);
    });
  }
  const int result = app.Run(window);
  WindowManager::GetInstance().RemoveListener(closed);
  tray->SetVisible(false);
  return result;
}
