import { common } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import nativeTests from 'libnativeapi_smoke.so';
interface ManagerSnapshot { count: number; current: number; hits: number[]; }
interface ManagerReply { passed: number; failed: number; samples: number; ui: ManagerSnapshot; worker: ManagerSnapshot; refreshedUi: ManagerSnapshot; }
interface ManagerState { ready: boolean; ids: number[]; system: number[]; alive: number[]; }
@Entry
@Component
struct WindowManagerTest {
  @State result: string = 'Preparing manager queries';
  private pause(): Promise<void> { return new Promise<void>((resolve) => { setTimeout(resolve, 350); }); }
  private async run(): Promise<void> {
    try {
      const main = await window.getLastWindow(this.getUIContext().getHostContext() as common.UIAbilityContext);
      nativeTests.managerState('setup');
      for (let i = 0; i < 100; ++i) {
        if ((JSON.parse(nativeTests.managerState('')) as ManagerState).ready) break;
        await new Promise<void>((resolve) => { setTimeout(resolve, 100); });
      }
      const initial = JSON.parse(nativeTests.managerState('')) as ManagerState;
      if (!initial.ready) throw new Error('Manager children did not attach');
      const a = window.findWindow('NativeAPI Manager A');
      const b = window.findWindow('NativeAPI Manager B');
      const rect = main.getWindowProperties().windowRect;
      const width = Math.min(700, rect.width - 100), height = Math.min(500, rect.height - 180);
      if (width <= 100 || height <= 100) throw new Error('No usable common child area');
      for (const child of [a, b]) {
        await child.resize(width, height); await child.moveWindowTo(rect.left + 60, rect.top + 100);
      }
      const phases = ['baseline', 'main-focus', 'z-a', 'z-b', 'raise-a', 'hide-b', 'show-b', 'destroy-b', 'close-a'];
      let bAlive = true, aAlive = true;
      const originalSystem = initial.system;
      for (const phase of phases) {
        let actionCode = 0;
        try {
          if (phase === 'main-focus') {
            const source = a.isFocused() ? a : b.isFocused() ? b : undefined;
            if (source) await window.shiftAppWindowFocus(source.getWindowProperties().id, main.getWindowProperties().id);
          } else if (phase === 'z-a' || phase === 'z-b') {
            await a.setSubWindowZLevel(phase === 'z-a' ? 5 : -5);
            await b.setSubWindowZLevel(phase === 'z-a' ? -5 : 5);
          } else if (phase === 'raise-a') {
            await a.setSubWindowZLevel(0); await b.setSubWindowZLevel(0); await a.raiseToAppTop();
          } else if (phase === 'hide-b') await b.minimize();
          else if (phase === 'show-b') await b.showWindow({focusOnShow: false});
          else if (phase === 'destroy-b') { await b.destroyWindow(); bAlive = false; }
          else if (phase === 'close-a') {
            nativeTests.managerState('close-a');
            for (let i = 0; i < 100; ++i) {
              if ((JSON.parse(nativeTests.managerState('')) as ManagerState).alive[1] === 0) { aAlive = false; break; }
              await new Promise<void>((resolve) => { setTimeout(resolve, 50); });
            }
            if (aAlive) throw new Error('Native close did not finish');
          }
        } catch (error) { actionCode = error.code; }
        if (actionCode !== 0 && actionCode !== 801 && actionCode !== 1300004) throw new Error(`Unexpected SDK action rejection ${actionCode}`);
        await this.pause();
        const scale = main.getWindowDensityInfo().systemDensity;
        const px = rect.left + 60 + Math.floor(width / 2), py = rect.top + 100 + Math.floor(height / 2);
        let sdkCode = 0;
        let order: number[] = [];
        try {
          const windows = await window.getWindowsByCoordinate(main.getWindowProperties().displayId, 0, px, py);
          order = windows.map((w: window.Window) => w.getWindowProperties().id).filter((id: number) => originalSystem.includes(id));
        } catch (error) { sdkCode = error.code; }
        if (sdkCode !== 0 && sdkCode !== 801) throw new Error(`Unexpected SDK query rejection ${sdkCode}`);
        const focused = main.isFocused() ? originalSystem[0] : aAlive && a.isFocused() ? originalSystem[1] : bAlive && b.isFocused() ? originalSystem[2] : originalSystem[0];
        const native = JSON.parse(await nativeTests.managerQuery(px / scale, py / scale)) as ManagerReply;
        this.result = `${phase}: ${JSON.stringify(native)}`;
        console.info(`NATIVEAPI_MANAGER_PHASE ${phase} ${JSON.stringify({native:native, order:order, sdkCode:sdkCode,
          actionCode:actionCode, system:originalSystem, focused:focused, alive:1 + (aAlive ? 1 : 0) + (bAlive ? 1 : 0), point:[px,py], scale:scale})}`);
      }
      console.info('NATIVEAPI_MANAGER_DONE');
    } catch (error) { console.error(`NATIVEAPI_MANAGER_ERROR ${error.code} ${error.message}`); }
  }
  aboutToAppear(): void { setTimeout(() => { this.run(); }, 400); }
  build() { Column({space:20}) { Text('NativeAPI Window Manager').fontSize(22)
    Text(this.result).fontSize(14)
  }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF') }
}
