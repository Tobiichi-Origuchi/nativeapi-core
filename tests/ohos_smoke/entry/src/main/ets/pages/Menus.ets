import { window } from '@kit.ArkUI';
import { common } from '@kit.AbilityKit';
import { NativeMenuHost, NativeMenuAnchorState, NativeMenuAnchorView } from '../nativeapi/NativeMenuHost';
import nativeTests from 'libnativeapi_smoke.so';
@Entry
@Component
struct Menus {
  @State result: string = 'NativeAPI menu tests running';
  private host?: NativeMenuHost;
  @State anchor: NativeMenuAnchorState = new NativeMenuAnchorState();
  aboutToAppear(): void {
    this.host = new NativeMenuHost(this.getUIContext(), 'nativeapi-menu-anchor', this.anchor);
    const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
    window.getLastWindow(context).then((owner: window.Window) => {
      if (!nativeTests.attachMenuHost(owner, this.host!)) { this.result = 'NATIVEAPI_MENU_ATTACH_FAILED'; return; }
      setTimeout(() => { nativeTests.runMenus().then((result: string) => { this.result = result; }); }, 800);
    }).catch(() => { this.result = 'NATIVEAPI_MENU_WINDOW_FAILED'; });
  }
  aboutToDisappear(): void { this.host?.dispose(); }
  build() {
    Stack({ alignContent: Alignment.TopStart }) {
      Column({ space: 16 }) {
        Text('NativeAPI Menu Acceptance').fontSize(24)
        Text(this.result).fontSize(16)
      }.padding(24).width('100%')
      NativeMenuAnchorView({ state: this.anchor, anchorId: 'nativeapi-menu-anchor' })
    }.width('100%').height('100%')
  }
}
