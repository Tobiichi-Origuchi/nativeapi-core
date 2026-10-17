import { common } from '@kit.AbilityKit';
import { image } from '@kit.ImageKit';
import { notificationManager } from '@kit.NotificationKit';
import { BusinessError } from '@kit.BasicServicesKit';
import nativeTests from 'libnativeapi_smoke.so';
interface IntegrationState {
  ok: boolean; desktop: boolean; visible: boolean; main: boolean; child: boolean;
  ready: boolean; tray: boolean; activated: number; deactivated: number; passed: number; failed: number;
}
@Entry
@Component
struct Integrations {
  @State result: string = 'Starting native application integration checks';
  @State busy: boolean = true;
  @State desktop: boolean = false;
  private state(check: string = ''): IntegrationState {
    this.result = nativeTests.integrationState(check);
    return JSON.parse(this.result) as IntegrationState;
  }
  private async settled(visible: boolean): Promise<void> {
    for (let i = 0; i < 100; ++i) {
      const state = this.state();
      if (state.visible === visible && state.main === visible && (!this.desktop || state.child === visible)) { return; }
      await new Promise<void>((resolve) => { setTimeout(resolve, 100); });
    }
    throw new Error(`Actual application windows failed to reach visible=${visible}`);
  }
  private async phoneSdk(): Promise<void> {
    const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
    for (const kind of ['hide', 'show', 'icon']) {
      let code = 0;
      try {
        if (kind === 'hide') { await context.hideAbility(); }
        if (kind === 'show') { await context.showAbility(); }
        if (kind === 'icon') {
          const pixels = image.createPixelMapSync(new Uint8Array(4 * 4 * 4).buffer,
            { size: { width: 4, height: 4 }, pixelFormat: image.PixelMapFormat.RGBA_8888 });
          await context.setMissionWindowIcon(pixels);
        }
      } catch (error) { code = (error as BusinessError).code; }
      console.info(`NATIVEAPI_INTEGRATION_PHONE_SDK ${kind} code=${code}`);
      if (code !== 801) { throw new Error(`Unexpected direct phone SDK ${kind}: ${code}`); }
    }
  }
  private async badge(expected: number): Promise<void> {
    for (let i = 0; i < 50; ++i) {
      const actual = await notificationManager.getBadgeNumber();
      if (actual === expected) {
        const settings = await notificationManager.getNotificationSetting();
        console.info(`NATIVEAPI_INTEGRATION_BADGE actual=${actual} settings=${JSON.stringify(settings)}`);
        return;
      }
      await new Promise<void>((resolve) => { setTimeout(resolve, 100); });
    }
    throw new Error('Actual SDK badge did not change');
  }
  private async action(command: string, useC: boolean, worker: boolean, restore: string = 'show'): Promise<void> {
    if (this.busy) { return; } this.busy = true;
    try {
      this.result = await nativeTests.integrationAction(command, useC, worker);
      if (command === 'badge9' || command === 'badge99' || command === 'clear') {
        await this.badge(command === 'badge9' ? 9 : command === 'badge99' ? 100 : 0);
      }
      if (command === 'hide' && this.desktop) {
        await this.settled(false); this.state('hidden');
        console.info(`NATIVEAPI_INTEGRATION_HIDDEN ${this.result}`);
        if (restore === 'external' || restore === 'tray') {
          await this.settled(true); this.state('shown');
          console.info(`NATIVEAPI_INTEGRATION_${restore.toUpperCase()}_RESTORED ${this.result}`);
          this.busy = false; return;
        }
        setTimeout(async () => {
          try {
            this.result = await nativeTests.integrationAction(restore, !useC, !worker);
            await this.settled(true); this.state('shown');
            console.info(`NATIVEAPI_INTEGRATION_RESTORED ${this.result}`);
          } catch (error) { console.error(`NATIVEAPI_INTEGRATION_ERROR ${error}`); }
          this.busy = false;
        }, 6000);
        return;
      }
      await new Promise<void>((resolve) => { setTimeout(resolve, 500); });
      console.info(`NATIVEAPI_INTEGRATION_DONE ${command} ${this.result}`);
    } catch (error) { this.result = `NATIVEAPI_INTEGRATION_ERROR ${error}`; console.error(this.result); }
    this.busy = false;
  }
  aboutToAppear(): void {
    setTimeout(async () => {
      try {
        this.result = nativeTests.setupIntegrations();
        this.desktop = (JSON.parse(this.result) as IntegrationState).desktop;
        for (let i = 0; i < 100; ++i) {
          const s = this.state();
          if (s.ready && (!this.desktop || (s.tray && s.child))) { break; }
          await new Promise<void>((resolve) => { setTimeout(resolve, 100); });
        }
        if (!this.desktop) {
          await this.phoneSdk();
          for (const command of ['hide', 'show', 'icon-a', 'icon-b']) {
            this.result = await nativeTests.integrationAction(command, command === 'show' || command === 'icon-b', true);
          }
        }
        console.info(`NATIVEAPI_INTEGRATION_READY ${this.result}`);
      } catch (error) { this.result = `NATIVEAPI_INTEGRATION_ERROR ${error}`; console.error(this.result); }
      this.busy = false;
    }, 700);
  }
  build() {
    Column({ space: 10 }) {
      Text('NativeAPI Application Integration Acceptance').fontSize(22)
      Text(this.result).fontSize(14)
      Button('NativeAPI Badge 9 C++').id('nativeapi-integration-badge9').enabled(!this.busy)
        .onClick(() => { this.action('badge9', false, false); })
      Button('NativeAPI Badge 99+ C worker').id('nativeapi-integration-badge99').enabled(!this.busy)
        .onClick(() => { this.action('badge99', true, true); })
      Button('NativeAPI Clear badge').id('nativeapi-integration-clear').enabled(!this.busy)
        .onClick(() => { this.action('clear', false, true); })
      if (this.desktop) {
      Button('NativeAPI Icon A C++').id('nativeapi-integration-icon-a').enabled(!this.busy)
        .onClick(() => { this.action('icon-a', false, false); })
      Button('NativeAPI Icon B C worker').id('nativeapi-integration-icon-b').enabled(!this.busy)
        .onClick(() => { this.action('icon-b', true, true); })
      Button('NativeAPI Hide C++').id('nativeapi-integration-hide-cpp').enabled(!this.busy)
        .onClick(() => { this.action('hide', false, false); })
      Button('NativeAPI Hide C worker').id('nativeapi-integration-hide-c').enabled(!this.busy)
        .onClick(() => { this.action('hide', true, true); })
      Button('NativeAPI Restore via window').id('nativeapi-integration-hide-window').enabled(!this.busy)
        .onClick(() => { this.action('hide', false, false, 'window-show'); })
      Button('NativeAPI Restore via launcher').id('nativeapi-integration-hide-external').enabled(!this.busy)
        .onClick(() => { this.action('hide', true, true, 'external'); })
      Button('NativeAPI Restore via status icon').id('nativeapi-integration-hide-tray').enabled(!this.busy)
        .onClick(() => { this.action('hide', false, true, 'tray'); })
      } else {
      Button('NativeAPI Preview phone badge').id('nativeapi-integration-preview-badge').enabled(!this.busy && !this.desktop)
        .onClick(() => {
          (this.getUIContext().getHostContext() as common.UIAbilityContext).moveAbilityToBackground()
            .then(() => { console.info('NATIVEAPI_INTEGRATION_BADGE_PREVIEW'); })
            .catch((error: BusinessError) => { console.error(`NATIVEAPI_INTEGRATION_ERROR preview ${error.code}`); });
        })
      }
      Button('NativeAPI Finish integrations').id('nativeapi-integration-finish').enabled(!this.busy)
        .onClick(() => { this.state('finish'); })
    }.padding(24).width('100%').height('100%')
  }
}
