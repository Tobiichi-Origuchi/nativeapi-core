# Tray colours

A C++ window with a native **Follow system colours** checkbox. It starts enabled:
the monochrome tray icon follows the menu bar or taskbar foreground. Turn it off
to restore the source image's blue colour. The tray context menu offers the same
toggle, Show window and Quit. Closing the window also exits the example.

```sh
cmake -S core -B core/build
cmake --build core/build --target tray_color_example --config Debug
```

Run the product for your platform:

- macOS: `open core/build/examples/tray_color_example/tray_color_example.app`
- Windows: `core\build\examples\tray_color_example\Debug\tray_color_example.exe`
- Linux: `core/build/examples/tray_color_example/tray_color_example`

Pass `--self-test` to exercise the checkbox's native callbacks (off, then on),
verify the tray mode and status text, check the source image stays unchanged,
and exit with a nonzero code if any check fails. No mouse or keyboard input is sent.

The checkbox is an example-local native control: AppKit / Win32 buttons and a GTK
check button. It doesn't add a public control API. All adaptive tinting is provided
by `TrayIcon::SetIconTemplate()`; independently themed Linux panels may differ from
the desktop preference.
