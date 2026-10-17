import { window } from '@kit.ArkUI';
import { common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import nativeTests from 'libnativeapi_smoke.so';
interface NativeStyle { ready: boolean; alive: boolean; preference: number; shadow: boolean; mainShadow: boolean; passed: number; failed: number; }
@Entry
@Component
struct WindowStyle {
  @State result: string = 'Preparing native window style';
  @State ready: boolean = false;
  @State busy: boolean = false;
  @State awaiting: boolean = false;
  private target?: window.Window;
  private main?: window.Window;
  private mainShadowAvailable: boolean = false;
  private initial: number = -1;
  private density: number = 1;
  private state(check: string = ''): NativeStyle { return JSON.parse(nativeTests.styleState(check)) as NativeStyle; }
  aboutToAppear(): void {
    setTimeout(async () => {
      try {
        const main = await window.getLastWindow(this.getUIContext().getHostContext() as common.UIAbilityContext);
        this.main = main;
        let cornerCode = 0;
        try { await main.setWindowCornerRadius(8); } catch (error) { cornerCode = (error as BusinessError).code; }
        console.info(`NATIVEAPI_STYLE_MAIN_CORNER_CODE ${cornerCode}`);
        if (cornerCode !== 1300004 && cornerCode !== 801) { throw new Error(`Unexpected main corner SDK result ${cornerCode}`); }
        let shadowCode = 0;
        try { await main.setWindowShadowEnabled(false); await main.setWindowShadowEnabled(true); }
        catch (error) { shadowCode = (error as BusinessError).code; }
        this.mainShadowAvailable = shadowCode === 0;
        console.info(`NATIVEAPI_STYLE_MAIN_SHADOW_CODE ${shadowCode}`);
        if (shadowCode !== 0 && shadowCode !== 201 && shadowCode !== 801) { throw new Error(`Unexpected shadow SDK result ${shadowCode}`); }
        nativeTests.styleState('setup');
        for (let i = 0; i < 100 && !this.state().ready; ++i) {
          await new Promise<void>((resolve) => { setTimeout(resolve, 100); });
        }
        if (!this.state().ready) { throw new Error('Style child not ready'); }
        this.target = window.findWindow('NativeAPI Window Style');
        this.initial = this.target.getWindowCornerRadius();
        this.density = this.target.getWindowDensityInfo().systemDensity;
        console.info(`NATIVEAPI_STYLE_READY initial=${this.initial} density=${this.density} target=${JSON.stringify(this.target.getWindowProperties())}`);
        this.ready = true; this.result = 'NativeAPI native window style ready';
      } catch (error) { this.result = `NATIVEAPI_STYLE_ERROR setup ${error}`; console.error(this.result); }
    }, 500);
  }
  private async settle(preference: number, radius: number, shadow: boolean): Promise<void> {
    if (!this.target) { throw new Error('Missing target'); }
    for (let i = 0; i < 100; ++i) {
      const current = this.state();
      if (current.preference === preference && current.shadow === shadow && Math.abs(this.target.getWindowCornerRadius() - radius) < 0.001) {
        if (current.failed !== 0) { throw new Error('Native style assertion failed'); } return;
      }
      await new Promise<void>((resolve) => { setTimeout(resolve, 100); });
    }
    throw new Error(`Native/SDK style failed to settle ${JSON.stringify(this.state())}`);
  }
  private async run(): Promise<void> {
    if (!this.target || this.busy || !this.ready) { return; } this.busy = true;
    try {
      this.initial = this.target.getWindowCornerRadius();
      console.info(`NATIVEAPI_STYLE_ORIGINAL ${this.initial}`);
      const phases = ['pending', 'small', 'round', 'default', 'external-corner', 'round', 'square', 'shadow-off', 'shadow-on', 'reference-off', 'reference-on', 'shadow-off', 'shadow-on', 'default'];
      for (let i = 0; i < phases.length; ++i) {
        const phase = phases[i];
        if (phase === 'external-corner') { await this.target.setWindowCornerRadius(12); }
        else if (phase === 'reference-off' || phase === 'reference-on') { this.target.setWindowShadowRadius(phase === 'reference-off' ? 0 : 18 * this.density); }
        else { this.result = await nativeTests.styleAction(phase, i === 1 || i === 5 || i === 8 || i === 11, i === 1 || i === 3 || i === 6 || i === 8 || i === 12 || i === 13); }
        const preference = i === 0 || i >= 6 && i < 13 ? 1 : phase === 'small' ? 3 : phase === 'round' ? 2 : 0;
        const radius = phase === 'external-corner' ? 12 : preference === 0 ? this.initial : preference === 1 ? 0 : preference === 2 ? 8 : 4;
        const shadow = i === 8 || i === 9 || i === 10 || i === 12 || i === 13;
        await this.settle(preference, radius, shadow);
        this.result = nativeTests.styleState('verify');
        await new Promise<void>((resolve) => { setTimeout(resolve, 350); });
        this.awaiting = true;
        console.info(`NATIVEAPI_STYLE_PHASE ${i} ${phase} radius=${this.target.getWindowCornerRadius()} native=${this.result} target=${JSON.stringify(this.target.getWindowProperties())}`);
        for (let j = 0; j < 400 && this.awaiting; ++j) { await new Promise<void>((resolve) => { setTimeout(resolve, 100); }); }
        if (this.awaiting) { throw new Error('Style scene acknowledgement timed out'); }
      }
      await nativeTests.styleAction('close', false, false);
      for (let j = 0; j < 100 && this.state().alive; ++j) { await new Promise<void>((resolve) => { setTimeout(resolve, 100); }); }
      if (this.mainShadowAvailable && this.main) {
        await nativeTests.styleAction('main-shadow-on', false, true);
        for (let j = 0; j < 100 && !this.state().mainShadow; ++j) { await new Promise<void>((resolve) => { setTimeout(resolve, 100); }); }
        if (!this.state().mainShadow) { throw new Error('Failed to establish native main shadow state'); }
        const mainPhases = ['main-shadow-off', 'main-shadow-on', 'main-reference-off', 'main-reference-on', 'main-shadow-off'];
        for (let i = 0; i < mainPhases.length; ++i) {
          const phase = mainPhases[i];
          if (phase === 'main-reference-off' || phase === 'main-reference-on') {
            await this.main.setWindowShadowEnabled(phase === 'main-reference-on');
          } else {
            await nativeTests.styleAction(phase, i === 1, i === 1 || i === 4);
          }
          const expected = i !== 0 && i !== 4;
          for (let j = 0; j < 100 && this.state().mainShadow !== expected; ++j) {
            await new Promise<void>((resolve) => { setTimeout(resolve, 100); });
          }
          if (this.state().mainShadow !== expected || this.state().failed !== 0) { throw new Error('Main native shadow did not settle'); }
          nativeTests.styleState('verify-main');
          await new Promise<void>((resolve) => { setTimeout(resolve, 350); });
          this.awaiting = true;
          console.info(`NATIVEAPI_STYLE_MAIN_PHASE ${i} ${phase} native=${JSON.stringify(this.state())} target=${JSON.stringify(this.main.getWindowProperties())}`);
          for (let j = 0; j < 400 && this.awaiting; ++j) { await new Promise<void>((resolve) => { setTimeout(resolve, 100); }); }
          if (this.awaiting) { throw new Error('Main shadow scene acknowledgement timed out'); }
        }
        await nativeTests.styleAction('main-shadow-on', false, false);
        for (let j = 0; j < 100 && !this.state().mainShadow; ++j) { await new Promise<void>((resolve) => { setTimeout(resolve, 100); }); }
        if (!this.state().mainShadow) { throw new Error('Failed to restore main shadow'); }
      }
      this.result = nativeTests.styleState('finish');
      console.info('NATIVEAPI_STYLE_UI_COMPLETE');
    } catch (error) { this.result = `NATIVEAPI_STYLE_ERROR ${error}`; console.error(this.result); }
    this.busy = false;
  }
  build() {
    Column({ space: 12 }) {
      Text('NativeAPI Window Style Acceptance').fontSize(22)
      Blank()
      Text(this.result).fontSize(14)
      Button('NativeAPI Next Window Style').id('nativeapi-style-next').enabled(this.awaiting).onClick(() => { this.awaiting = false; })
      Button('NativeAPI Run Window Style').id('nativeapi-style-run').enabled(this.ready && !this.busy).onClick(() => { this.run(); })
    }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF')
  }
}
