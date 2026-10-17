import { window } from '@kit.ArkUI';
import nativeTests from 'libnativeapi_smoke.so';
import { WindowShadowFixture } from './WindowShadowFixture';

interface LayoutBounds { x: number; y: number; width: number; height: number; }
interface LayoutTarget { id: number; alive: boolean; bounds: LayoutBounds; events: number[]; }
interface LayoutBase { passed: number; failed: number; windows: LayoutTarget[]; }
interface LayoutFull { native: LayoutBase; calls: number; fullscreen: boolean[]; events: number[][][]; }
interface LayoutNative { native: LayoutFull; calls: number; result: number; observed: number; layout: number[]; pending: number[]; held: number[][]; sdkCalls: number[][]; content: LayoutBounds[]; }
interface LayoutSdk { id: number; windowRect: window.Rect; drawableRect: window.Rect; isFullScreen: boolean; isLayoutFullScreen: boolean; }
interface LayoutSnapshot { native: LayoutNative; windows: (LayoutSdk | undefined)[]; status: number[]; density: number[]; }

@Entry
@Component
struct WindowLayout {
  @State result: string = 'Preparing immersive content layout';
  private targets: window.Window[] = [];
  private pendingJob?: Promise<boolean>;
  private async pause(): Promise<void> { await new Promise<void>((resolve) => { setTimeout(resolve, 150); }); }
  private state(command: string = ''): LayoutNative { return JSON.parse(nativeTests.layoutState(command)) as LayoutNative; }
  private snapshot(): LayoutSnapshot {
    const properties: (LayoutSdk | undefined)[] = []; const status: number[] = []; const density: number[] = [];
    for (const target of this.targets) {
      try { const p = target.getWindowProperties(); properties.push({ id: p.id, windowRect: p.windowRect, drawableRect: p.drawableRect, isFullScreen: p.isFullScreen, isLayoutFullScreen: p.isLayoutFullScreen }); } catch (_error) { properties.push(undefined); }
      try { status.push(target.getWindowStatus()); } catch (_error) { status.push(-1); }
      try { density.push(target.getWindowDensityInfo().systemDensity); } catch (_error) { density.push(1); }
    }
    return { native: this.state(), windows: properties, status: status, density: density };
  }
  private async settle(): Promise<void> {
    let stable = 0;
    for (let i = 0; i < 100; ++i) {
      const s = this.snapshot(); let same = s.native.pending.every((n: number) => n === 0);
      for (let j = 0; j < 3; ++j) {
        const p = s.windows[j]; const n = s.native.native.native.windows[j];
        if (p === undefined) { same = same && !n.alive && s.native.layout[j] === 0; continue; }
        same = same && s.native.layout[j] === Number(p.isLayoutFullScreen) &&
          Math.abs(n.bounds.x * s.density[j] - p.windowRect.left) < 0.03 &&
          Math.abs(n.bounds.y * s.density[j] - p.windowRect.top) < 0.03 &&
          Math.abs(n.bounds.width * s.density[j] - p.windowRect.width) < 0.03 &&
          Math.abs(n.bounds.height * s.density[j] - p.windowRect.height) < 0.03;
      }
      stable = same ? stable + 1 : 0; if (stable >= 3) return; await this.pause();
    }
    throw new Error(`Layout state did not settle ${JSON.stringify(this.snapshot())}`);
  }
  private async waitHeld(): Promise<void> {
    for (let i = 0; i < 100; ++i) { const n = this.state(); if (n.pending[1] === 1 && n.held[1][1] === 1) return; await this.pause(); }
    throw new Error('Actual SDK layout result not held');
  }
  private async ack(serial: number): Promise<void> {
    for (let i = 0; i < 220 && AppStorage.get<string>('nativeapiLayoutAck') !== String(serial); ++i) await this.pause();
    if (AppStorage.get<string>('nativeapiLayoutAck') !== String(serial)) throw new Error('Layout acknowledgement timeout');
  }
  private async run(): Promise<void> {
    try {
      const stage = WindowShadowFixture.stage; if (!stage) throw new Error('Missing Stage');
      const main = stage.getMainWindowSync(); const available = main.isInFreeWindowMode();
      if (available && main.getWindowStatus() !== 4) await main.recover();
      nativeTests.layoutState('setup');
      const child = await stage.createSubWindowWithOptions('NativeAPI Layout Child', { title: 'NativeAPI Layout Child', decorEnabled: true, maximizeSupported: true });
      const ordinary = await stage.createSubWindowWithOptions('NativeAPI Ordinary Layout', { title: 'NativeAPI Ordinary Layout', decorEnabled: true });
      this.targets = [main, child, ordinary];
      await child.setUIContent('pages/ControlsTarget'); await ordinary.setUIContent('pages/ControlsTarget');
      await child.resize(800, 500); await child.moveWindowTo(350, 500); await child.showWindow();
      await ordinary.resize(300, 180); await ordinary.moveWindowTo(750, 650); await ordinary.showWindow();
      nativeTests.layoutState('attach-child', child); nativeTests.layoutState('attach-ordinary', ordinary);
      const enabled: LayoutSdk[] = [];
      for (const target of this.targets) {
        await target.setWindowLayoutFullScreen(true); await this.pause();
        const p = target.getWindowProperties(); enabled.push({ id: p.id, windowRect: p.windowRect, drawableRect: p.drawableRect, isFullScreen: p.isFullScreen, isLayoutFullScreen: p.isLayoutFullScreen });
        await target.setWindowLayoutFullScreen(false); await this.pause();
      }
      this.state('watch'); await this.settle();
      const bases = this.targets.map((target: window.Window) => target.getWindowProperties().windowRect);
      console.info(`NATIVEAPI_LAYOUT_REFERENCE ${JSON.stringify({ available: available, bases: bases, enabled: enabled, status: this.snapshot().status })}`);
      const phases: string[] = ['baseline'];
      for (const name of ['main', 'child', 'ordinary']) for (const action of ['cpp-ui-enable', 'c-worker-disable', 'c-ui-enable', 'cpp-worker-disable', 'cpp-worker-repeat-disable', 'c-worker-enable', 'cpp-ui-disable', 'sdk-enable', 'sdk-disable']) phases.push(`${name}-${action}`);
      phases.push('fault-on', 'fault-cpp-ui', 'fault-c-worker', 'nonpromise-on', 'nonpromise-c-ui', 'nonpromise-cpp-worker', 'normal',
        'hold-ui', 'overlap-c-ui', 'overlap-cpp-worker', 'release-ui', 'retry-c-worker', 'hold-worker', 'close-children', 'late-sdk-result', 'closed-c-worker', 'invalid', 'finish');
      for (let i = 0; i < phases.length; ++i) {
        const phase = phases[i]; console.info(`NATIVEAPI_LAYOUT_BEFORE ${i} ${phase} ${JSON.stringify(this.snapshot())}`);
        let accepted = -1; const index = phase.startsWith('main-') ? 0 : phase.startsWith('ordinary-') ? 2 : 1;
        if (phase.endsWith('sdk-enable') || phase.endsWith('sdk-disable')) await this.targets[index].setWindowLayoutFullScreen(phase.endsWith('sdk-enable'));
        else if (phase.startsWith('fault-') && phase !== 'fault-on' || phase.startsWith('nonpromise-') && phase !== 'nonpromise-on')
          accepted = Number(await nativeTests.layoutAction(1, true, phase.includes('-c-'), phase.includes('worker')));
        else if (phase === 'fault-on') this.state('fault');
        else if (phase === 'nonpromise-on') this.state('nonpromise');
        else if (phase === 'normal') this.state('normal');
        else if (phase === 'hold-ui') { this.state('hold'); accepted = Number(await nativeTests.layoutAction(1, true, false, false)); await this.waitHeld(); }
        else if (phase === 'overlap-c-ui' || phase === 'overlap-cpp-worker') accepted = Number(await nativeTests.layoutAction(1, false, phase.includes('-c-'), phase.includes('worker')));
        else if (phase === 'release-ui') this.state('release');
        else if (phase === 'retry-c-worker') accepted = Number(await nativeTests.layoutAction(1, false, true, true));
        else if (phase === 'hold-worker') { this.state('hold'); this.pendingJob = nativeTests.layoutAction(1, true, true, true); await this.waitHeld(); }
        else if (phase === 'close-children') { await child.destroyWindow(); await ordinary.destroyWindow(); if (!this.pendingJob) throw new Error('Missing held worker'); accepted = Number(await this.pendingJob); }
        else if (phase === 'late-sdk-result') this.state('release');
        else if (phase === 'closed-c-worker') accepted = Number(await nativeTests.layoutAction(1, false, true, true));
        else if (phase === 'invalid') this.state('invalid');
        else if (phase.includes('-enable') || phase.includes('-disable')) accepted = Number(await nativeTests.layoutAction(index, phase.endsWith('-enable'), phase.includes('-c-'), phase.includes('worker')));
        if (!['hold-ui', 'overlap-c-ui', 'overlap-cpp-worker', 'hold-worker'].includes(phase)) await this.settle(); else await this.pause();
        if (phase === 'finish') this.state('finish'); else this.state('verify');
        this.result = phase;
        console.info(`NATIVEAPI_LAYOUT_PHASE ${i} ${phase} ${JSON.stringify({ snapshot: this.snapshot(), accepted: accepted, target: index })}`);
        await this.ack(i + 1);
      }
      this.result = 'Immersive content layout acceptance passed'; console.info('NATIVEAPI_LAYOUT_DONE');
    } catch (error) { console.error(`NATIVEAPI_LAYOUT_ERROR ${error.code} ${error.message}`); }
  }
  aboutToAppear(): void { setTimeout(() => { this.run(); }, 450); }
  build() { Column() { Text('NativeAPI immersive content layout'); Text(this.result) }.width('100%').height('100%').backgroundColor('#FFFFFF') }
}
