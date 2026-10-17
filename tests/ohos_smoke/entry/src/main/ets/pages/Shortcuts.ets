import { window } from '@kit.ArkUI';
import nativeTests from 'libnativeapi_smoke.so';
import { NativeShortcutHost } from '../nativeapi/NativeShortcutHost';
@Entry
@Component
struct Shortcuts {
  @StorageLink('nativeapiLocalKeyHost') @Watch('hostChanged') localHost: boolean = false;
  private host?: NativeShortcutHost;
  private hostChanged(): void {
    if (this.host) { this.host.dispose(); this.host = undefined; }
    if (this.localHost) {
      const name = AppStorage.get<string>('nativeapiShortcutWindowName');
      if (!name) { throw new Error('Missing shortcut window name'); }
      this.host = new NativeShortcutHost(window.findWindow(name), nativeTests);
    }
    console.info(`NATIVEAPI_LOCAL_HOST ${this.localHost}`);
  }
  aboutToAppear(): void { setTimeout(() => { console.info('NATIVEAPI_SHORTCUT_READY'); }, 500); }
  aboutToDisappear(): void { if (this.host) { this.host.dispose(); this.host = undefined; } }
  build() { Column({ space: 20 }) { Text('NativeAPI Shortcut Input').fontSize(22)
    Button('Local input focus').defaultFocus(true)
    Text('SDK reservations and independent application conflicts').fontSize(14)
  }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF')
    .onKeyPreIme((event: KeyEvent): boolean => this.host?.handle(event) ?? false) }
}
