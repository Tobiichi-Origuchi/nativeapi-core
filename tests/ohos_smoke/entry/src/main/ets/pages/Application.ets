import { common } from '@kit.AbilityKit';
import nativeTests from 'libnativeapi_smoke.so';
@Entry
@Component
struct Application {
  @State result: string = 'Starting application checks';
  @StorageLink('nativeapiAppColorMode') colorMode: number = -1;
  private readColor(label: string): void {
    setTimeout(() => {
      const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
      const color = context.resourceManager.getColorSync($r('sys.color.ohos_id_color_background').id);
      console.info(`NATIVEAPI_APP_THEME ${label} resource=${color.toString(16)} mode=${this.colorMode}`);
      this.result = `NativeAPI application theme ${label}`;
    }, 500);
  }
  private theme(mode: number, useC: boolean, worker: boolean, label: string): void {
    nativeTests.setApplicationTheme(mode, useC, worker).then((ok: boolean) => {
      if (!ok) { this.result = 'NATIVEAPI_APP_THEME_FAIL'; return; }
      this.readColor(label);
    });
  }
  aboutToAppear(): void {
    setTimeout(() => { this.result = nativeTests.runApplication(); this.readColor('initial'); }, 500);
  }
  build() {
    Column({ space: 12 }) {
      Text('NativeAPI Application Acceptance').fontSize(24)
      Text(this.result).fontSize(16)
      Text('NativeAPI theme swatch').fontSize(18).height(100).width('100%')
        .backgroundColor($r('sys.color.ohos_id_color_background'))
      Button('NativeAPI Light C++').onClick(() => { this.theme(1, false, false, 'light'); })
      Button('NativeAPI Dark C ABI').onClick(() => { this.theme(2, true, false, 'dark'); })
      Button('NativeAPI System worker').onClick(() => { this.theme(0, true, true, 'system'); })
      Button('NativeAPI Cancel quit').onClick(() => { nativeTests.runApplicationQuit(1); })
      Button('NativeAPI Deferred cancel').onClick(() => { nativeTests.runApplicationQuit(2); })
      Button('NativeAPI Accept quit').onClick(() => { nativeTests.runApplicationQuit(3); })
      Button('NativeAPI SDK terminate').onClick(() => {
        nativeTests.runApplicationQuit(4);
        (this.getUIContext().getHostContext() as common.UIAbilityContext).terminateSelf();
      })
    }.padding(24).width('100%').height('100%')
      .backgroundColor($r('sys.color.ohos_id_color_background'))
  }
}
