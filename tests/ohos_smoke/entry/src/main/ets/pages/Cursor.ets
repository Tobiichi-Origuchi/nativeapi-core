import { window } from '@kit.ArkUI';
import { common } from '@kit.AbilityKit';
import nativeTests from 'libnativeapi_smoke.so';

@Entry
@Component
struct Cursor {
  @State result: string = 'Ready for real pointer queries';
  private main?: window.Window;
  aboutToAppear(): void {
    setTimeout(async () => {
      this.main = await window.getLastWindow(this.getUIContext().getHostContext() as common.UIAbilityContext);
      console.info(`NATIVEAPI_CURSOR_READY ${JSON.stringify(this.main.getWindowProperties())}`);
    }, 500);
  }
  private sample(phase: string, event: ClickEvent): void {
    this.result = nativeTests.cursorProbe(phase, event.globalDisplayX, event.globalDisplayY);
  }
  private async changeFocus(): Promise<void> {
    if (!this.main) { return; }
    try {
      await this.main.setWindowFocusable(false);
      await new Promise<void>((resolve) => { setTimeout(resolve, 350); });
      nativeTests.cursorProbe('unfocused', 0, 0);
      await new Promise<void>((resolve) => { setTimeout(resolve, 3000); });
    } catch (error) { console.error(`NATIVEAPI_CURSOR_ERROR focus ${JSON.stringify(error)}`); }
    finally {
      await this.main.setWindowFocusable(true);
      await this.main.showWindow();
      console.info('NATIVEAPI_CURSOR_RESTORED');
    }
  }
  build() {
    Column({ space: 24 }) {
      Text('NativeAPI Cursor Acceptance').fontSize(22)
      Row() {
        Button('Sample A').id('nativeapi-cursor-a').onClick((event: ClickEvent) => { this.sample('a', event); })
        Blank()
      }.width('100%')
      Blank()
      Row() {
        Blank()
        Button('Sample B').id('nativeapi-cursor-b').onClick((event: ClickEvent) => { this.sample('b', event); })
      }.width('100%')
      Text(this.result).fontSize(12).maxLines(5)
      Row() {
        Button('Sample C').id('nativeapi-cursor-c').onClick((event: ClickEvent) => { this.sample('c', event); })
        Blank()
        Button('Test focus refusal').id('nativeapi-cursor-blur').onClick(() => { this.changeFocus(); })
      }.width('100%')
    }.width('100%').height('100%').padding(36).backgroundColor('#FFFFFF')
  }
}
