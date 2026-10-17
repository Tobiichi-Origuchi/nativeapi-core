import { window } from '@kit.ArkUI';
import { common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import nativeTests from 'libnativeapi_smoke.so';
interface AspectState { ready: boolean; alive: boolean; ratio: number; pending: number; passed: number; failed: number; }
@Entry
@Component
struct WindowAspect {
  @State result: string = 'Preparing aspect constraints';
  @State ready: boolean = false;
  @State busy: boolean = false;
  @State awaiting: boolean = false;
  private main?: window.Window;
  private child?: window.Window;
  private available: boolean = false;
  private desktop: boolean = false;
  private originalLimits?: window.WindowLimits;
  private state(command: string = ''): AspectState { return JSON.parse(nativeTests.aspectState(command)) as AspectState; }
  private async settle(): Promise<void> {
    for (let j = 0; j < 150 && this.state().pending > 0; ++j) {
      await new Promise<void>((resolve) => { setTimeout(resolve, 50); });
    }
    if (this.state().pending > 0) { throw new Error('Native aspect requests did not settle'); }
    await new Promise<void>((resolve) => { setTimeout(resolve, 250); });
  }
  aboutToAppear(): void {
    setTimeout(async () => {
      try {
        this.main = await window.getLastWindow(this.getUIContext().getHostContext() as common.UIAbilityContext);
        this.desktop = this.main.getWindowStatus() === window.WindowStatusType.FLOATING;
        if (this.desktop) { this.originalLimits = this.main.getWindowLimits(); }
        let code = 0;
        try { await this.main.setContentAspectRatio(1.5, false, false); }
        catch (error) { code = (error as BusinessError).code; }
        console.info(`NATIVEAPI_ASPECT_MAIN_SDK_CODE ${code}`);
        if (code !== 0 && code !== 801 && code !== 1300004) { throw new Error('Unexpected main aspect capability result'); }
        this.available = code === 0;
        if (this.available) { await this.main.resetAspectRatio(); }
        if (this.desktop) { await this.main.setResizeByDragEnabled(true); }
        nativeTests.aspectState('setup');
        for (let j = 0; j < 100 && !this.state().ready; ++j) {
          await new Promise<void>((resolve) => { setTimeout(resolve, 100); });
        }
        if (!this.state().ready) { throw new Error('Aspect child did not become ready'); }
        this.child = window.findWindow('NativeAPI Aspect Child');
        let childCode = 0;
        try { await this.child.setContentAspectRatio(1.5, false, false); }
        catch (error) { childCode = (error as BusinessError).code; }
        console.info(`NATIVEAPI_ASPECT_CHILD_SDK_CODE ${childCode}`);
        if (childCode !== 801 && childCode !== 1300004) { throw new Error('Unexpected child aspect capability result'); }
        this.ready = true;
        console.info(`NATIVEAPI_ASPECT_READY available=${this.available} desktop=${this.desktop} main=${JSON.stringify(this.main.getWindowProperties())} child=${JSON.stringify(this.child.getWindowProperties())}`);
      } catch (error) { console.error(`NATIVEAPI_ASPECT_ERROR setup ${error}`); }
    }, 500);
  }
  private async run(): Promise<void> {
    if (!this.main || !this.child || !this.ready || this.busy) { return; }
    this.busy = true;
    try {
      const phases = ['baseline', 'square', 'programmatic', 'wide', 'invalid', 'reject', 'queued', 'sdk', 'restore-library', 'hide-decor', 'show-decor', 'clear', 'negative', 'child', 'limited', 'restore'];
      const ratios = [0, 1, 1, 1.5, 1.5, 1.5, 1.25, 1.25, 1.25, 1.25, 1.25, 0, 0, 0, 1.5, 0];
      for (let i = 0; i < phases.length; ++i) {
        const phase = phases[i];
        if (phase === 'restore' && this.originalLimits) { await this.main.setWindowLimits(this.originalLimits); }
        if (this.desktop) { await this.main.resize(1500, 1050); await this.main.moveWindowTo(500, 250); }
        await new Promise<void>((resolve) => { setTimeout(resolve, 250); });
        const before = this.main.getWindowProperties();
        if (phase === 'limited') {
          if (this.desktop) {
            await this.main.setWindowLimits({ minWidth: 800, minHeight: 500, maxWidth: 1600, maxHeight: 1500 });
            console.info(`NATIVEAPI_ASPECT_LIMITS ${JSON.stringify(this.main.getWindowLimits())}`);
          }
          await nativeTests.aspectAction('wide', true, true);
        } else if (phase === 'sdk') {
          if (this.available) { await this.main.setContentAspectRatio(2, false, false); }
        } else if (phase !== 'baseline' && (phase !== 'programmatic' || this.desktop)) {
          const immediate = JSON.parse(await nativeTests.aspectAction(phase, phase === 'wide' || phase === 'clear', phase === 'wide' || phase === 'invalid' || phase === 'child' || phase === 'clear')) as AspectState;
          if (phase === 'queued' && this.available && (immediate.pending !== 3 || immediate.ratio !== 1.5)) {
            throw new Error('Queued UI requests reported success before SDK completion');
          }
        }
        await this.settle();
        const state = this.state('verify');
        if (state.failed || state.ratio !== (this.available ? ratios[i] : 0)) {
          throw new Error(`Unexpected native aspect state at ${phase}: ${JSON.stringify(state)}`);
        }
        this.result = `${phase} ${JSON.stringify(state)}`;
        this.awaiting = true;
        console.info(`NATIVEAPI_ASPECT_PHASE ${i} ${phase} native=${JSON.stringify(state)} before=${JSON.stringify(before)} target=${JSON.stringify(this.main.getWindowProperties())}`);
        for (let j = 0; j < 400 && this.awaiting; ++j) {
          await new Promise<void>((resolve) => { setTimeout(resolve, 100); });
        }
        if (this.awaiting) { throw new Error('Aspect acknowledgement timed out'); }
      }
      await nativeTests.aspectAction('close', false, false);
      for (let j = 0; j < 100 && this.state().alive; ++j) {
        await new Promise<void>((resolve) => { setTimeout(resolve, 100); });
      }
      this.result = nativeTests.aspectState('finish');
      console.info('NATIVEAPI_ASPECT_UI_COMPLETE');
    } catch (error) { this.result = `NATIVEAPI_ASPECT_ERROR ${error}`; console.error(this.result); }
    this.busy = false;
  }
  build() {
    Column({ space: 12 }) {
      Text('NativeAPI Window Aspect Acceptance').fontSize(22)
      Blank()
      Text(this.result).fontSize(12)
      Button('NativeAPI Next Aspect').id('nativeapi-aspect-next').enabled(this.awaiting).onClick(() => { this.awaiting = false; })
      Button('NativeAPI Run Aspect').id('nativeapi-aspect-run').enabled(this.ready && !this.busy).onClick(() => { this.run(); })
    }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF')
  }
}
