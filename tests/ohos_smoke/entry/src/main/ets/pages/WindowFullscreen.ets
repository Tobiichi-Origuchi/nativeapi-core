import { window } from '@kit.ArkUI';
import { BusinessError } from '@kit.BasicServicesKit';
import { bundleManager } from '@kit.AbilityKit';
import nativeTests from 'libnativeapi_smoke.so';
import { WindowShadowFixture } from './WindowShadowFixture';

interface FullNativeBounds { x: number; y: number; width: number; height: number; }
interface FullNativeTarget { id: number; alive: boolean; bounds: FullNativeBounds; events: number[]; }
interface FullNativeBase { passed: number; failed: number; windows: FullNativeTarget[]; }
interface FullNativeState { native: FullNativeBase; calls: number; fullscreen: boolean[]; events: number[][][]; }
interface FullSdkWindow { id: number; windowRect: window.Rect; isFullScreen: boolean; isLayoutFullScreen: boolean; }
interface FullSnapshot { native: FullNativeState; windows: (FullSdkWindow | undefined)[]; status: number[]; density: number[]; freeform: boolean[]; }

@Entry
@Component
struct WindowFullscreen {
  @State result: string = 'Preparing fullscreen acceptance';
  private targets: window.Window[] = [];
  private async pause(): Promise<void> { await new Promise<void>((resolve) => { setTimeout(resolve, 180); }); }
  private state(command: string = ''): FullNativeState { return JSON.parse(nativeTests.fullscreenState(command)) as FullNativeState; }
  private snapshot(): FullSnapshot {
    const properties: (FullSdkWindow | undefined)[] = [];
    const status: number[] = []; const density: number[] = []; const freeform: boolean[] = [];
    for (const target of this.targets) {
      try { const p = target.getWindowProperties(); properties.push({ id: p.id, windowRect: p.windowRect, isFullScreen: p.isFullScreen, isLayoutFullScreen: p.isLayoutFullScreen }); } catch (_error) { properties.push(undefined); }
      try { status.push(target.getWindowStatus()); } catch (_error) { status.push(-1); }
      try { density.push(target.getWindowDensityInfo().systemDensity); } catch (_error) { density.push(1); }
      try { freeform.push(target.isInFreeWindowMode()); } catch (_error) { freeform.push(false); }
    }
    return { native: this.state(), windows: properties, status: status, density: density, freeform: freeform };
  }
  private async settle(index: number, wanted: boolean, status: number, rect?: window.Rect): Promise<void> {
    let stable = 0;
    for (let i = 0; i < 100; ++i) {
      const snapshot = this.snapshot(); const p = snapshot.windows[index]; const n = snapshot.native.native.windows[index];
      const same = p !== undefined && snapshot.native.fullscreen[index] === wanted && snapshot.status[index] === status &&
        Math.abs(n.bounds.x * snapshot.density[index] - p.windowRect.left) < 0.03 &&
        Math.abs(n.bounds.y * snapshot.density[index] - p.windowRect.top) < 0.03 &&
        Math.abs(n.bounds.width * snapshot.density[index] - p.windowRect.width) < 0.03 &&
        Math.abs(n.bounds.height * snapshot.density[index] - p.windowRect.height) < 0.03 &&
        (rect === undefined || JSON.stringify(p.windowRect) === JSON.stringify(rect));
      stable = same ? stable + 1 : 0;
      if (stable >= 3) return;
      await this.pause();
    }
    throw new Error(`Fullscreen failed to settle ${index} ${wanted} ${status} ${JSON.stringify(this.snapshot())}`);
  }
  private async ack(serial: number): Promise<void> {
    for (let i = 0; i < 200 && AppStorage.get<string>('nativeapiFullscreenAck') !== String(serial); ++i) await this.pause();
    if (AppStorage.get<string>('nativeapiFullscreenAck') !== String(serial)) throw new Error('Fullscreen acknowledgement timeout');
  }
  private async run(): Promise<void> {
    try {
      const stage = WindowShadowFixture.stage;
      if (!stage) throw new Error('Missing fixture Stage');
      const main = stage.getMainWindowSync();
      const available = main.isInFreeWindowMode();
      if (available && main.getWindowStatus() !== window.WindowStatusType.FLOATING) await main.recover();
      nativeTests.fullscreenState('setup');
      const child = await stage.createSubWindowWithOptions('NativeAPI Fullscreen Child', { title: 'NativeAPI Fullscreen Child', decorEnabled: true, maximizeSupported: true });
      const ordinary = await stage.createSubWindowWithOptions('NativeAPI Ordinary Fullscreen Child', { title: 'NativeAPI Ordinary Fullscreen Child', decorEnabled: true });
      this.targets = [main, child, ordinary];
      await child.setUIContent('pages/ControlsTarget'); await ordinary.setUIContent('pages/ControlsTarget');
      await child.resize(800, 500); await child.moveWindowTo(350, 500); await child.showWindow();
      await ordinary.resize(300, 180); await ordinary.moveWindowTo(750, 650); await ordinary.showWindow();
      nativeTests.fullscreenState('attach-child', child); nativeTests.fullscreenState('attach-ordinary', ordinary);
      await nativeTests.fullscreenAction(0, false, false, false); await this.pause();
      nativeTests.fullscreenState('watch');
      const bases = [main.getWindowProperties().windowRect, child.getWindowProperties().windowRect, ordinary.getWindowProperties().windowRect];
      const initialMainStatus = main.getWindowStatus();
      console.info(`NATIVEAPI_FULLSCREEN_REFERENCE ${JSON.stringify({ available: available, bases: bases, mainStatus: initialMainStatus })}`);
      const phases = ['baseline', 'main-cpp-ui-enter', 'main-c-ui-repeat-enter', 'main-c-ui-exit', 'main-cpp-worker-repeat-exit', 'main-c-worker-enter', 'main-cpp-worker-exit',
        'child-cpp-ui-enter', 'child-c-worker-repeat-enter', 'child-c-ui-exit', 'child-cpp-ui-repeat-exit', 'child-c-worker-enter', 'child-cpp-worker-exit',
        'child-maximized', 'child-cpp-worker-enter-maximized', 'child-c-ui-exit-maximized', 'child-recover',
        'ordinary-cpp-enter', 'ordinary-c-worker-enter', 'sdk-child-enter', 'sdk-child-exit',
        'child-restrict-modes', 'child-restricted-enter', 'child-restore-modes', 'child-restored-enter', 'child-restored-exit',
        'close-children', 'closed-cpp-ui', 'closed-c-worker', 'invalid', 'finish'];
      for (let i = 0; i < phases.length; ++i) {
        const phase = phases[i]; const before = this.snapshot(); let code = 0; let index = phase.startsWith('main') ? 0 : 1;
        let expected = before.native.fullscreen[index]; let wantedStatus = before.status[index]; let rect: window.Rect | undefined = bases[index];
        try {
          if (phase === 'child-maximized' && available) { await child.maximize(window.MaximizePresentation.EXIT_IMMERSIVE); wantedStatus = 2; rect = undefined; }
          else if (phase === 'child-recover' && available) { await child.recover(); wantedStatus = 4; }
          else if (phase === 'sdk-child-enter') {
            await child.maximize(window.MaximizePresentation.ENTER_IMMERSIVE); expected = available; wantedStatus = available ? 1 : 4; rect = available ? undefined : bases[1];
          } else if (phase === 'sdk-child-exit') { await child.recover(); expected = false; wantedStatus = 4; }
          else if (phase === 'child-restrict-modes') await child.setSupportedWindowModes([bundleManager.SupportWindowMode.FLOATING]);
          else if (phase === 'child-restore-modes') await child.setSupportedWindowModes([bundleManager.SupportWindowMode.FULL_SCREEN, bundleManager.SupportWindowMode.FLOATING]);
          else if (phase === 'ordinary-cpp-enter' || phase === 'ordinary-c-worker-enter') {
            index = 2; rect = bases[2]; expected = !available; wantedStatus = 4; await nativeTests.fullscreenAction(2, true, phase.includes('-c-'), phase.includes('worker'));
          } else if (phase === 'close-children') { await child.destroyWindow(); await ordinary.destroyWindow(); }
          else if (phase === 'closed-cpp-ui' || phase === 'closed-c-worker') await nativeTests.fullscreenAction(1, true, phase.includes('-c-'), phase.includes('worker'));
          else if (phase === 'invalid') nativeTests.fullscreenState('invalid');
          else if (phase.includes('-enter') || phase.includes('-exit')) {
            const enter = phase.includes('-enter');
            await nativeTests.fullscreenAction(index, enter, phase.includes('-c-'), phase.includes('worker'));
            expected = enter && (!available || phase !== 'child-restricted-enter');
            wantedStatus = index === 0 ? (available && enter ? 1 : initialMainStatus) : (available && expected ? 1 : 4);
            if (phase === 'child-c-ui-exit-maximized' && available) wantedStatus = 2;
            if (available && (enter && expected || wantedStatus === 2)) rect = undefined;
          }
        } catch (error) { code = (error as BusinessError).code; }
        if (i < phases.indexOf('close-children')) await this.settle(index, expected, wantedStatus, rect); else await this.pause();
        if (phase !== 'finish') this.state('verify');
        else this.state('finish');
        this.result = phase;
        console.info(`NATIVEAPI_FULLSCREEN_PHASE ${i} ${phase} ${JSON.stringify({ before: before, snapshot: this.snapshot(), code: code, target: index })}`);
        await this.ack(i + 1);
      }
      this.result = 'Fullscreen acceptance passed'; console.info('NATIVEAPI_FULLSCREEN_DONE');
    } catch (error) { console.error(`NATIVEAPI_FULLSCREEN_ERROR ${error.code} ${error.message}`); }
  }
  aboutToAppear(): void { setTimeout(() => { this.run(); }, 450); }
  build() { Column() { Text('NativeAPI fullscreen acceptance'); Text(this.result) }.width('100%').height('100%').backgroundColor('#FFFFFF') }
}
