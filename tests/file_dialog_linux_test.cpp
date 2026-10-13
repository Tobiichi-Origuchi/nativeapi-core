// Explicit integration test: run in a private Xvfb session with GTK_USE_PORTAL unset,
// so GtkFileChooserNative falls back to the in-process GtkFileChooserDialog it drives.
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include "../src/file_dialog.h"
#include "../src/window.h"

using namespace nativeapi;
namespace {
bool failed = false;
// Records instead of throwing: most checks run inside GTK callbacks.
void Check(bool value, const std::string& label) {
  std::cout << (value ? "PASS " : "FAIL ") << label << std::endl;
  failed = failed || !value;
}
GtkFileChooser* FindChooser() {
  GList* windows = gtk_window_list_toplevels();
  GtkFileChooser* found = nullptr;
  for (GList* item = windows; item; item = item->next) {
    if (GTK_IS_FILE_CHOOSER_DIALOG(item->data) && gtk_widget_get_visible(GTK_WIDGET(item->data)))
      found = GTK_FILE_CHOOSER(item->data);
  }
  g_list_free(windows);
  return found;
}
struct Driver {
  // Called with the tick index (0 on the first tick the chooser is mapped);
  // returns true once it has finished.
  std::function<bool(GtkFileChooser*, int)> act;
  int ticks = 0;
  bool finished = false;
};
// Polls until the chooser is mapped, then acts on it from inside FileDialog::Open().
gboolean Drive(gpointer data) {
  auto* driver = static_cast<Driver*>(data);
  GtkFileChooser* chooser = FindChooser();
  if (!chooser)
    return G_SOURCE_CONTINUE;
  if (driver->act(chooser, driver->ticks++) || driver->ticks > 100) {
    if (driver->ticks > 100)  // give up without hanging Open()
      gtk_dialog_response(GTK_DIALOG(chooser), GTK_RESPONSE_CANCEL);
    driver->finished = true;
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}
void Run(FileDialog& dialog, std::function<bool(GtkFileChooser*, int)> act) {
  Driver driver{std::move(act)};
  const guint source = g_timeout_add(50, Drive, &driver);
  Check(dialog.Open(), "Open returns true: " + dialog.GetLastError());
  if (!driver.finished)
    g_source_remove(source);
}
guint SelectedCount(GtkFileChooser* chooser) {
  GSList* selected = gtk_file_chooser_get_filenames(chooser);
  const guint count = g_slist_length(selected);
  g_slist_free_full(selected, g_free);
  return count;
}
void Accept(GtkFileChooser* chooser) {
  gtk_dialog_response(GTK_DIALOG(chooser), GTK_RESPONSE_ACCEPT);
}
}  // namespace

int main() {
  if (g_getenv("GTK_USE_PORTAL") || !gtk_init_check(nullptr, nullptr))
    return 77;
  char* root = g_dir_make_tmp("nativeapi-file-dialog-XXXXXX", nullptr);
  const std::string dir = root;
  g_free(root);
  const std::string a = dir + "/a.txt", b = dir + "/b.MD", sub = dir + "/folder";
  std::ofstream(a) << "a";
  std::ofstream(b) << "b";
  g_mkdir(sub.c_str(), 0700);
  {
    Check(FileDialog::IsSupported(), "supported");
    auto window = std::make_shared<Window>();
    window->SetTitle("file dialog test");
    window->Show();
    auto* owner = GTK_WINDOW(gtk_widget_get_toplevel(static_cast<GtkWidget*>(window->GetNativeObject())));

    // Multiple selection, with a case-insensitive extension filter.
    {
      FileDialog dialog(FileDialogMode::OpenFiles);
      Check(dialog.SetParentWindow(window) && dialog.SetFileTypes({".txt", ".md"}), "configure");
      Run(dialog, [&](GtkFileChooser* chooser, int tick) {
        if (tick > 0) {
          // select_filename replaces the selection in GTK 3; select_all extends it
          // to every file the filter shows (a.txt, b.MD) once the folder has loaded.
          gtk_file_chooser_select_all(chooser);
          if (SelectedCount(chooser) < 2)
            return false;
          Accept(chooser);
          return true;
        }
        Check(gtk_window_get_transient_for(GTK_WINDOW(chooser)) == owner, "transient for parent");
        Check(gtk_window_get_modal(GTK_WINDOW(chooser)), "modal");
        Check(gtk_file_chooser_get_select_multiple(chooser), "select multiple");
        Check(!dialog.SetFileTypes({".png"}) && !dialog.Open(), "locked while open");
        GtkFileFilterInfo info{};
        info.contains = GTK_FILE_FILTER_DISPLAY_NAME;
        info.display_name = "B.MD";
        Check(gtk_file_filter_filter(gtk_file_chooser_get_filter(chooser), &info), "filter ignores case");
        gtk_file_chooser_set_current_folder(chooser, dir.c_str());
        return false;
      });
      auto paths = dialog.GetPaths();
      std::sort(paths.begin(), paths.end());
      Check(dialog.GetResult() == FileDialogResult::Accepted && paths == std::vector<std::string>{a, b},
            "multiple files accepted");
    }

    // Folder selection.
    {
      FileDialog dialog(FileDialogMode::SelectFolder);
      dialog.SetParentWindow(window);
      Run(dialog, [&](GtkFileChooser* chooser, int tick) {
        if (tick == 0) {
          Check(gtk_file_chooser_get_action(chooser) == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                "folder action");
          gtk_file_chooser_set_current_folder(chooser, dir.c_str());
          return false;
        }
        gtk_file_chooser_select_filename(chooser, sub.c_str());
        char* current = gtk_file_chooser_get_filename(chooser);
        const bool ready = current && sub == current;
        g_free(current);
        if (!ready)
          return false;
        Accept(chooser);
        return true;
      });
      Check(dialog.GetResult() == FileDialogResult::Accepted &&
                dialog.GetPaths() == std::vector<std::string>{sub},
            "folder accepted");
    }

    // Save: suggested name gets the first extension; overwrite is confirmed.
    {
      FileDialog dialog(FileDialogMode::SaveFile);
      dialog.SetFileTypes({".md"});
      dialog.SetSuggestedFileName("notes");
      Run(dialog, [&](GtkFileChooser* chooser, int) {
        char* name = gtk_file_chooser_get_current_name(chooser);
        Check(name && std::string(name) == "notes.md", "suggested name");
        g_free(name);
        Check(gtk_file_chooser_get_do_overwrite_confirmation(chooser), "overwrite confirmation");
        gtk_dialog_response(GTK_DIALOG(chooser), GTK_RESPONSE_CANCEL);
        return true;
      });
      Check(dialog.GetResult() == FileDialogResult::Cancelled && dialog.GetPaths().empty(),
            "save cancelled by the user");
    }

    // Programmatic Close() in every mode.
    for (auto mode : {FileDialogMode::OpenFile, FileDialogMode::OpenFiles, FileDialogMode::SaveFile,
                      FileDialogMode::SelectFolder}) {
      FileDialog dialog(mode);
      dialog.SetParentWindow(window);
      Check(!dialog.Close(), "Close before Open is refused");
      Run(dialog, [&](GtkFileChooser*, int) { return dialog.Close(); });
      Check(dialog.GetResult() == FileDialogResult::Cancelled && dialog.GetPaths().empty() &&
                FindChooser() == nullptr,
            "Close cancels mode " + std::to_string(static_cast<int>(mode)));
    }
  }
  g_remove(a.c_str());
  g_remove(b.c_str());
  g_rmdir(sub.c_str());
  g_rmdir(dir.c_str());
  if (failed)
    return 1;
  std::cout << "File dialog tests passed" << std::endl;
  return 0;
}
