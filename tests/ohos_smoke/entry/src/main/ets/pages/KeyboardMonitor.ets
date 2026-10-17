@Entry
@Component
struct KeyboardMonitor {
  aboutToAppear(): void { setTimeout(() => { console.warn('NATIVEAPI_MONITOR_READY'); }, 500); }
  build() { Column({ space: 20 }) {
    Text('NativeAPI Keyboard Monitor').fontSize(22)
    Button('Keyboard test focus').defaultFocus(true)
    Text('Global native key events and monitor lifecycle').fontSize(14)
  }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF')
    .onKeyPreIme((event: KeyEvent): boolean => {
      if (event.keyCode === 2098) {
        console.warn(`NATIVEAPI_MONITOR_COMPONENT_F9 type=${event.type} ctrl=${event.getModifierKeyState?.(['Ctrl'])} alt=${event.getModifierKeyState?.(['Alt'])}`);
      }
      return false;
    }) }
}
