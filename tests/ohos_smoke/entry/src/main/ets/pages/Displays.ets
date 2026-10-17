import { window } from '@kit.ArkUI';
import { common } from '@kit.AbilityKit';
import nativeTests from 'libnativeapi_smoke.so';
@Entry
@Component
struct Displays {
  @State result: string = 'Preparing display observations';
  private command(action: string): void { this.result = nativeTests.displayState(action); }
  aboutToAppear(): void {
    setTimeout(async () => {
      try {
        const main = await window.getLastWindow(this.getUIContext().getHostContext() as common.UIAbilityContext);
        await main.setPreferredOrientation(window.Orientation.AUTO_ROTATION);
        this.command('setup');
        console.info('NATIVEAPI_DISPLAY_READY');
      } catch (error) { console.error(`NATIVEAPI_DISPLAY_ERROR setup ${JSON.stringify(error)}`); }
    }, 500);
  }
  build() {
    Column({ space: 12 }) {
      Text('NativeAPI Display Acceptance').fontSize(22)
      Text(this.result).fontSize(11).maxLines(6)
      Row({ space: 12 }) {
        Button('Check').id('nativeapi-displays-verify').onClick(() => { this.command('verify'); })
        Button('Unlisten').id('nativeapi-displays-unlisten').onClick(() => { this.command('unlisten'); })
        Button('Listen').id('nativeapi-displays-listen').onClick(() => { this.command('listen'); })
      }
      Row({ space: 12 }) {
        Button('Remove C').id('nativeapi-displays-remove-c').onClick(() => { this.command('remove-c'); })
        Button('Hold queue').onClick(() => { this.command('hold-queue'); })
        Button('Replace observers').onClick(() => { this.command('replace-queue'); })
        Button('Finish').id('nativeapi-displays-finish').onClick(() => { this.command('finish'); })
      }
    }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF')
  }
}
