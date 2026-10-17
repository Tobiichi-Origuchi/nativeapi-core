import { NodeContent, window } from '@kit.ArkUI';
import { common } from '@kit.AbilityKit';
import nativeTests from 'libnativeapi_smoke.so';
import { NativeTooltipHost } from '../nativeapi/NativeTooltipHost';
interface TooltipState { passed: number; failed: number; clicks: number; primary: number; secondary: number; nested: number; field: number; requestAge?: number; }
class TooltipProbeHost extends NativeTooltipHost {
  requested: number = 0;
  show(key: number, node: number, text: string, valid: () => boolean): boolean {
    this.requested = Date.now();
    return super.show(key, node, text, valid);
  }
}
@Entry
@Component
struct TooltipTest {
  private content: NodeContent = new NodeContent();
  private host?: TooltipProbeHost;
  private owner?: window.Window;
  @StorageLink('nativeapiTooltipAction') @Watch('command') action: string = '';
  private command(): void {
    const parts = this.action.split(':');
    const sequence = parts[0], action = parts[1];
    if (!sequence || !action) return;
    if (action === 'detach') nativeTests.detachViewContent();
    else if ((action === 'reattach' || action === 'replace-host') && this.owner) {
      this.host = new TooltipProbeHost(this.getUIContext());
      if (!nativeTests.tooltipAttach(this.owner, this.content, this.host)) throw new Error('Tooltip reattach failed');
    } else nativeTests.tooltipCommand(action);
    const state = JSON.parse(nativeTests.tooltipCommand('')) as TooltipState;
    state.requestAge = this.host ? Date.now() - this.host.requested : -1;
    console.info(`NATIVEAPI_TOOLTIP_ACTION ${sequence} ${action} ${JSON.stringify(state)}`);
  }
  aboutToAppear(): void {
    window.getLastWindow(this.getUIContext().getHostContext() as common.UIAbilityContext).then((owner: window.Window) => {
      this.owner = owner;
      this.host = new TooltipProbeHost(this.getUIContext());
      if (!nativeTests.tooltipAttach(owner, this.content, this.host)) throw new Error('Tooltip attachment failed');
      console.info(`NATIVEAPI_TOOLTIP_READY ${nativeTests.tooltipCommand('setup')}`);
    }).catch((error: Error) => { console.error(`NATIVEAPI_TOOLTIP_ERROR ${error.message}`); });
  }
  aboutToDisappear(): void { nativeTests.detachViewContent(); this.host?.dispose(); }
  build() { Column() { ContentSlot(this.content) }.width('100%').height('100%').backgroundColor('#FFFFFF') }
}
