import { window } from '@kit.ArkUI';
import { bundleManager, common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import nativeTests from 'libnativeapi_smoke.so';
import { WindowShadowFixture } from './WindowShadowFixture';

interface ModeBounds { x: number; y: number; width: number; height: number; }
interface ModeTarget { id: number; alive: boolean; bounds: ModeBounds; events: number[]; }
interface ModeBase { passed: number; failed: number; windows: ModeTarget[]; }
interface ModeNative { native: ModeBase; calls: number; windowCalls: number; observed: boolean; policy: boolean[]; fullscreen: boolean[]; events: number[][]; masks: number[]; pending: number[]; sdkCalls: number[][]; held: number[][]; }
interface ModeSdk { id: number; windowRect: window.Rect; isFullScreen: boolean; isLayoutFullScreen: boolean; }
interface ModeSnapshot { native: ModeNative; windows: (ModeSdk | undefined)[]; status: number[]; density: number[]; }

@Entry
@Component
struct WindowModePolicy {
  @State result: string = 'Preparing supported-mode policies';
  private targets: window.Window[] = [];
  private available: boolean = false;
  private pendingJob?: Promise<string>;
  private async pause(): Promise<void> { await new Promise<void>((resolve) => { setTimeout(resolve, 180); }); }
  private state(command: string = ''): ModeNative { return JSON.parse(nativeTests.modeState(command)) as ModeNative; }
  private snapshot(): ModeSnapshot {
    const properties: (ModeSdk | undefined)[] = []; const status: number[] = []; const density: number[] = [];
    for (const target of this.targets) {
      try { const p = target.getWindowProperties(); properties.push({ id: p.id, windowRect: p.windowRect, isFullScreen: p.isFullScreen, isLayoutFullScreen: p.isLayoutFullScreen }); } catch (_error) { properties.push(undefined); }
      try { status.push(target.getWindowStatus()); } catch (_error) { status.push(-1); }
      try { density.push(target.getWindowDensityInfo().systemDensity); } catch (_error) { density.push(1); }
    }
    return { native: this.state(), windows: properties, status: status, density: density };
  }
  private async settle(policy: boolean[], index: number = -1, wantedStatus: number = -1): Promise<void> {
    let stable = 0;
    for (let i = 0; i < 100; ++i) {
      const s = this.snapshot(); let same = JSON.stringify(s.native.policy) === JSON.stringify(policy) && s.native.pending.every((n: number) => n === 0);
      if (index >= 0) same = same && s.status[index] === wantedStatus;
      for (let j = 0; j < 3; ++j) {
        const p = s.windows[j]; const n = s.native.native.windows[j];
        if (p === undefined) { same = same && !n.alive && !s.native.fullscreen[j]; continue; }
        same = same && s.native.fullscreen[j] === (this.available ? s.status[j] === 1 : p.isFullScreen) &&
          Math.abs(n.bounds.x * s.density[j] - p.windowRect.left) < 0.03 &&
          Math.abs(n.bounds.y * s.density[j] - p.windowRect.top) < 0.03 &&
          Math.abs(n.bounds.width * s.density[j] - p.windowRect.width) < 0.03 &&
          Math.abs(n.bounds.height * s.density[j] - p.windowRect.height) < 0.03;
      }
      stable = same ? stable + 1 : 0; if (stable >= 3) return;
      await this.pause();
    }
    throw new Error(`Mode state did not settle ${JSON.stringify(this.snapshot())}`);
  }
  private async ack(serial: number): Promise<void> {
    for (let i = 0; i < 200 && AppStorage.get<string>('nativeapiModeAck') !== String(serial); ++i) await this.pause();
    if (AppStorage.get<string>('nativeapiModeAck') !== String(serial)) throw new Error('Mode acknowledgement timeout');
  }
  private async policy(index: number, enabled: boolean, useC: boolean, worker: boolean, batch: boolean = false): Promise<void> {
    await nativeTests.modeAction(index, enabled, useC, worker, batch, 'policy');
  }
  private async full(index: number, enabled: boolean, useC: boolean = false, worker: boolean = false): Promise<void> {
    await nativeTests.modeAction(index, enabled, useC, worker, false, 'fullscreen');
  }
  private async run(): Promise<void> {
    try {
      const stage = WindowShadowFixture.stage; if (!stage) throw new Error('Missing Stage');
      const main = stage.getMainWindowSync(); this.available = main.isInFreeWindowMode();
      if (this.available && main.getWindowStatus() !== 4) await main.recover();
      const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
      const declared = context.abilityInfo.supportWindowModes;
      nativeTests.modeState('setup');
      const child = await stage.createSubWindowWithOptions('NativeAPI Mode Child', { title: 'NativeAPI Mode Child', decorEnabled: true, maximizeSupported: true });
      const ordinary = await stage.createSubWindowWithOptions('NativeAPI Ordinary Mode Policy', { title: 'NativeAPI Ordinary Mode Policy', decorEnabled: true });
      this.targets = [main, child, ordinary];
      await child.setUIContent('pages/ControlsTarget'); await ordinary.setUIContent('pages/ControlsTarget');
      await child.resize(800, 500); await child.moveWindowTo(350, 500); await child.showWindow();
      await ordinary.resize(300, 180); await ordinary.moveWindowTo(750, 650); await ordinary.showWindow();
      nativeTests.modeState('attach-child', child); nativeTests.modeState('attach-ordinary', ordinary);
      this.state('watch');
      const full = bundleManager.SupportWindowMode.FULL_SCREEN; const floating = bundleManager.SupportWindowMode.FLOATING;
      const bases = this.targets.map((target: window.Window) => target.getWindowProperties().windowRect);
      const policies: boolean[] = [declared.includes(full), false, false];
      console.info(`NATIVEAPI_MODE_REFERENCE ${JSON.stringify({ available: this.available, declared: declared, bases: bases })}`);
      const phases = ['baseline', 'main-cpp-ui-disable', 'main-disabled-sdk-full', 'main-disabled-native-full', 'main-native-exit',
        'main-c-worker-enable', 'main-native-enter', 'main-c-native-exit', 'main-ui-batch', 'main-worker-enable', 'main-repeat-enable',
        'fallback-on', 'fallback-disable', 'fallback-enable', 'fallback-off', 'host-main-floating-only', 'main-enable-no-split',
        'main-disable-no-split', 'host-main-restore', 'child-cpp-ui-enable', 'child-native-enter', 'child-native-exit',
        'child-c-worker-disable', 'child-disabled-sdk-full', 'child-worker-enable', 'child-ui-batch', 'child-worker-restore',
        'ordinary-cpp-enable', 'ordinary-native-enter', 'ordinary-native-exit', 'ordinary-c-disable', 'ordinary-disabled-sdk-full',
        'host-ordinary-full-only', 'ordinary-reject-empty', 'host-ordinary-restore', 'invalid-baselines', 'pending-prepare', 'close-children', 'late-sdk-completion',
        'closed-c-worker', 'closed-cpp-ui', 'invalid', 'finish'];
      for (let i = 0; i < phases.length; ++i) {
        const phase = phases[i]; console.info(`NATIVEAPI_MODE_BEFORE ${i} ${phase} ${JSON.stringify(this.snapshot())}`);
        let code = 0; let index = -1; let wanted = -1;
        try {
          if (phase === 'main-cpp-ui-disable') { await this.policy(0, false, false, false); if (this.available) policies[0] = false; }
          else if (phase === 'main-disabled-sdk-full') await main.maximize(window.MaximizePresentation.ENTER_IMMERSIVE);
          else if (phase === 'main-disabled-native-full') await this.full(0, true);
          else if (phase === 'main-native-exit' || phase === 'main-c-native-exit') { await this.full(0, false, phase.includes('-c-')); index = 0; wanted = this.available ? 4 : 1; }
          else if (phase === 'main-c-worker-enable' || phase === 'main-worker-enable') { await this.policy(0, true, true, true); if (this.available) policies[0] = true; }
          else if (phase === 'main-native-enter') { await this.full(0, true); index = 0; wanted = 1; }
          else if (phase === 'main-ui-batch') { await this.policy(0, false, false, false, true); if (this.available) policies[0] = false; }
          else if (phase === 'main-repeat-enable') await this.policy(0, true, false, false);
          else if (phase === 'fallback-on' || phase === 'fallback-off') this.state(phase);
          else if (phase === 'fallback-disable' || phase === 'fallback-enable') { const enable = phase === 'fallback-enable'; await this.policy(0, enable, true, true); if (this.available) policies[0] = enable; }
          else if (phase === 'host-main-floating-only') { await main.setSupportedWindowModes([floating]); if (!nativeTests.modeSeed(0, [floating])) throw new Error('Could not publish main SDK override'); policies[0] = false; }
          else if (phase === 'main-enable-no-split' || phase === 'main-disable-no-split') { const enable = phase === 'main-enable-no-split'; await this.policy(0, enable, true, phase.includes('disable')); if (this.available) policies[0] = enable; }
          else if (phase === 'host-main-restore') { await main.setSupportedWindowModes(declared); if (!nativeTests.modeSeed(0, declared)) throw new Error('Could not publish original main modes'); policies[0] = declared.includes(full); }
          else if (phase === 'child-cpp-ui-enable' || phase === 'child-worker-enable' || phase === 'child-worker-restore') { await this.policy(1, true, phase.includes('restore'), phase.includes('worker')); if (this.available) policies[1] = true; }
          else if (phase === 'child-native-enter' || phase === 'ordinary-native-enter') { index = phase.startsWith('child') ? 1 : 2; await this.full(index, true); wanted = this.available ? 1 : 4; }
          else if (phase === 'child-native-exit' || phase === 'ordinary-native-exit') { index = phase.startsWith('child') ? 1 : 2; await this.full(index, false, true, true); wanted = 4; }
          else if (phase === 'child-c-worker-disable') { await this.policy(1, false, true, true); if (this.available) policies[1] = false; }
          else if (phase === 'child-disabled-sdk-full') await child.maximize(window.MaximizePresentation.ENTER_IMMERSIVE);
          else if (phase === 'child-ui-batch') { await this.policy(1, false, true, false, true); if (this.available) policies[1] = false; }
          else if (phase === 'ordinary-cpp-enable') { await this.policy(2, true, false, false); if (this.available) policies[2] = true; }
          else if (phase === 'ordinary-c-disable') { await this.policy(2, false, true, true); if (this.available) policies[2] = false; }
          else if (phase === 'ordinary-disabled-sdk-full') await ordinary.maximize(window.MaximizePresentation.ENTER_IMMERSIVE);
          else if (phase === 'host-ordinary-full-only') { await ordinary.setSupportedWindowModes([full]); if (!nativeTests.modeSeed(2, [full])) throw new Error('Could not publish full-only SDK override'); policies[2] = true; if (ordinary.getWindowStatus() !== 1) await ordinary.maximize(window.MaximizePresentation.ENTER_IMMERSIVE); index = 2; wanted = 1; }
          else if (phase === 'ordinary-reject-empty') await this.policy(2, false, false, true);
          else if (phase === 'host-ordinary-restore') { await ordinary.setSupportedWindowModes([full, floating]); if (ordinary.getWindowStatus() !== 4) await ordinary.recover(); if (!nativeTests.modeSeed(2, [full, floating])) throw new Error('Could not publish ordinary recovery'); policies[2] = true; index = 2; wanted = 4; }
          else if (phase === 'invalid-baselines') { if (nativeTests.modeSeed(0, []) || nativeTests.modeSeed(0, [1]) || nativeTests.modeSeed(0, [2, 2]) || nativeTests.modeSeed(0, [3]) || nativeTests.modeSeed(1, [1, 2])) throw new Error('Invalid mode baseline accepted'); }
          else if (phase === 'pending-prepare') {
            this.state('hold-child'); await this.policy(1, false, false, false, true);
            this.pendingJob = nativeTests.modeAction(1, true, true, true, false, 'policy');
            for (let j = 0; j < 150; ++j) { const n = this.state(); if (n.pending[1] === 4 && n.held[1][1] === 1) break; await this.pause(); }
            const n = this.state(); if (n.pending[1] !== 4 || n.held[1][1] !== 1) throw new Error('No real held SDK result and waiting worker');
            if (nativeTests.modeSeed(1, [full, floating])) throw new Error('Host override replaced an in-flight request');
            await child.maximize(window.MaximizePresentation.ENTER_IMMERSIVE);
          }
          else if (phase === 'close-children') { await child.destroyWindow(); if (this.pendingJob) await this.pendingJob; await ordinary.destroyWindow(); policies[1] = policies[2] = false; }
          else if (phase === 'late-sdk-completion') { this.state('release-held'); await this.pause(); await this.pause(); }

          else if (phase === 'closed-c-worker' || phase === 'closed-cpp-ui') await this.policy(1, true, phase.includes('-c-'), phase.includes('worker'));
          else if (phase === 'invalid') this.state('invalid');
        } catch (error) { code = (error as BusinessError).code; if (code === undefined) throw new Error(`${phase}: ${error.message}`); }
        if (phase !== 'pending-prepare') await this.settle(policies, index, wanted);
        if (phase === 'finish') this.state('finish'); else this.state('verify');
        this.result = phase;
        console.info(`NATIVEAPI_MODE_PHASE ${i} ${phase} ${JSON.stringify({ snapshot: this.snapshot(), code: code })}`);
        await this.ack(i + 1);
      }
      this.result = 'Supported-mode policy acceptance passed'; console.info('NATIVEAPI_MODE_DONE');
    } catch (error) { console.error(`NATIVEAPI_MODE_ERROR ${error.code} ${error.message}`); }
  }
  aboutToAppear(): void { setTimeout(() => { this.run(); }, 450); }
  build() { Column() { Text('NativeAPI fullscreen mode policy'); Text(this.result) }.width('100%').height('100%').backgroundColor('#FFFFFF') }
}
