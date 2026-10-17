import { NodeContent, window } from '@kit.ArkUI';
import { common } from '@kit.AbilityKit';
import nativeTests from 'libnativeapi_smoke.so';
@Entry
@Component
struct Views {
  private content: NodeContent = new NodeContent();
  aboutToAppear(): void {
    const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
    window.getLastWindow(context).then((owner: window.Window) => {
      if (!nativeTests.attachViewContent(owner, this.content)) {
        console.error('NATIVEAPI_VIEW_ATTACH_FAILED');
        return;
      }
      setTimeout(() => { nativeTests.runViews(); }, 500);
    }).catch(() => { console.error('NATIVEAPI_VIEW_WINDOW_FAILED'); });
  }
  aboutToDisappear(): void { nativeTests.detachViewContent(); }
  build() { Column() { ContentSlot(this.content) }.width('100%').height('100%') }
}
