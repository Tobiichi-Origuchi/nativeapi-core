import { statusBarManager } from '@kit.DeskTopExtensionKit';
import { image } from '@kit.ImageKit';
import { common } from '@kit.AbilityKit';
import { BusinessError, emitter } from '@kit.BasicServicesKit';
@Entry
@Component
struct TrayReference {
  @State result: string = 'Checking native status bar service';
  private icon?: image.PixelMap;
  private click = (event: emitter.EventData): void => { console.info(`NATIVEAPI_TRAY_REF_CLICK ${JSON.stringify(event)}`); };
  private menu = (event: emitter.EventData): void => { console.info(`NATIVEAPI_TRAY_REF_MENU ${JSON.stringify(event)}`); };
  private hover = (event: emitter.EventData): void => { console.info(`NATIVEAPI_TRAY_REF_HOVER ${JSON.stringify(event)}`); };
  private context(): common.UIAbilityContext { return this.getUIContext().getHostContext() as common.UIAbilityContext; }
  private add(): void {
    const bytes = new Uint8Array(24 * 24 * 4);
    for (let i = 0; i < 24 * 24; ++i) {
      bytes[i * 4] = 255; bytes[i * 4 + 1] = 0; bytes[i * 4 + 2] = 255; bytes[i * 4 + 3] = 255;
    }
    image.createPixelMap(bytes.buffer, { size: { width: 24, height: 24 }, pixelFormat: image.PixelMapFormat.RGBA_8888 }).then((icon: image.PixelMap) => {
      this.icon = icon;
      const item: statusBarManager.StatusBarItem = {
        icons: { white: icon, black: icon },
        quickOperation: { title: 'NativeAPI Tray Reference', height: 100, abilityName: '', moduleName: 'entry' },
        hoverTips: 'NativeAPI Tray Reference',
        statusBarGroupMenu: [[{ title: 'NativeAPI Tray Action', menuCode: 'nativeapi-tray-action',
          menuAction: { abilityName: 'EntryAbility', moduleName: 'entry', notifyOnly: true } }]]
      };
      statusBarManager.addToStatusBar(this.context(), item, (error: BusinessError) => {
        this.result = `NATIVEAPI_TRAY_REF_ADD ${error?.code ?? 0}`;
        console.info(this.result);
      });
    }).catch((error: BusinessError) => { console.error(`NATIVEAPI_TRAY_REF_IMAGE ${error.code}`); });
  }
  aboutToAppear(): void {
    const syscap = canIUse('SystemCapability.PCService.StatusBarManager');
    console.info(`NATIVEAPI_TRAY_REF_SYSCAP ${syscap}`);
    if (!syscap) {
      this.result = 'NATIVEAPI_TRAY_REF_CAP false'; console.info(this.result); return;
    }
    statusBarManager.on('statusBarIconClick', this.click);
    statusBarManager.on('rightMenuClick', this.menu);
    statusBarManager.onIconHover(this.hover);
    statusBarManager.isStatusBarCapabilitySupported(this.context()).then((supported: boolean) => {
      this.result = `NATIVEAPI_TRAY_REF_CAP ${supported}`;
      console.info(this.result);
      if (supported) { setTimeout(() => { this.add(); }, 1000); }
    }).catch((error: BusinessError) => { this.result = `NATIVEAPI_TRAY_REF_CAP_ERROR ${error.code}`; console.error(this.result); });
  }
  build() {
    Column({ space: 16 }) {
      Text('NativeAPI Tray SDK Reference').fontSize(24)
      Text(this.result).fontSize(18)
      Button('NativeAPI SDK Hide Ability').id('nativeapi-sdk-hide').onClick(() => {
        this.context().hideAbility().then(() => {
          console.info('NATIVEAPI_SDK_HIDE_OK');
          setTimeout(() => {
            this.context().showAbility().then(() => { console.info('NATIVEAPI_SDK_SHOW_OK'); })
              .catch((error: BusinessError) => { console.error(`NATIVEAPI_SDK_SHOW_ERROR ${error.code}`); });
          }, 4000);
        }).catch((error: BusinessError) => { console.error(`NATIVEAPI_SDK_HIDE_ERROR ${error.code} ${error.message}`); });
      })
      Button('NativeAPI Remove SDK tray').onClick(() => {
        statusBarManager.removeFromStatusBar(this.context(), (error: BusinessError) => {
          this.result = `NATIVEAPI_TRAY_REF_REMOVE ${error?.code ?? 0}`; console.info(this.result);
        });
      })
    }.padding(24).width('100%').height('100%')
  }
}
