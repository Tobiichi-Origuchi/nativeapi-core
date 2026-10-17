import { window } from '@kit.ArkUI';
import nativeTests from 'libnativeapi_smoke.so';
interface NativeOcclusion { ready: boolean; state: number; objects: number; managers: number; cEvents: number; passed: number; failed: number; }
@Entry
@Component
struct Occlusion {
  @State result: string = 'Preparing actual SDK occlusion scene';
  @State ready: boolean = false;
  @State busy: boolean = false;
  @State awaiting: boolean = false;
  private sdk: number = -1;
  private target?: window.Window;
  private cover?: window.Window;
  private callback = (state: window.OcclusionState): void => {
    this.sdk = state;
    console.info(`NATIVEAPI_OCCLUSION_SDK ${state}`);
  };
  private state(): NativeOcclusion { return JSON.parse(nativeTests.occlusionCommand('state')) as NativeOcclusion; }
  private async wait(expected: number): Promise<void> {
    for (let i = 0; i < 100; ++i) {
      if (this.sdk === expected && this.state().state === (expected === 2 ? 2 : 1)) { return; }
      await new Promise<void>((resolve) => { setTimeout(resolve, 100); });
    }
    throw new Error(`Expected SDK=${expected}, actual=${this.sdk}, native=${JSON.stringify(this.state())}`);
  }
  aboutToAppear(): void {
    setTimeout(async () => {
      try {
        nativeTests.occlusionCommand('setup');
        for (let i = 0; i < 100 && !this.state().ready; ++i) {
          await new Promise<void>((resolve) => { setTimeout(resolve, 100); });
        }
        if (!this.state().ready) { throw new Error('Real child windows not ready'); }
        this.target = window.findWindow('NativeAPI Occlusion Target');
        this.cover = window.findWindow('NativeAPI Occlusion Cover');
        this.target.setWindowShadowRadius(0);
        this.cover.setWindowShadowRadius(0);
        this.target.on('occlusionStateChanged', this.callback);
        console.info(`NATIVEAPI_OCCLUSION_READY ${JSON.stringify(this.target.getWindowProperties())}`);
        this.ready = true; this.result = 'NativeAPI occlusion ready';
      } catch (error) { this.result = `NATIVEAPI_OCCLUSION_ERROR setup ${error}`; console.error(this.result); }
    }, 500);
  }
  private async run(): Promise<void> {
    if (!this.ready || this.busy || !this.target || !this.cover) { return; }
    this.busy = true;
    try {
      const phases: string[] = ['uncovered', 'partial', 'full', 'transparent', 'full-again', 'uncovered', 'hidden', 'reshown', 'unlisten', 'unobserved-full', 'unobserved-visible', 'relisten', 'hidden', 'reshown', 'close'];
      for (let i = 0; i < phases.length; ++i) {
        const phase = phases[i];
        const before = this.state();
        nativeTests.occlusionCommand(phase);
        if (phase === 'close') {
          for (let j = 0; j < 100 && this.state().state !== 0; ++j) {
            await new Promise<void>((resolve) => { setTimeout(resolve, 100); });
          }
          this.result = await nativeTests.occlusionCheck(0, true, false);
        } else if (phase === 'unlisten' || phase === 'relisten') {
          await new Promise<void>((resolve) => { setTimeout(resolve, 300); });
          const after = this.state();
          if (after.objects !== before.objects || after.managers !== before.managers || after.cEvents !== before.cEvents) {
            throw new Error('Listener mutation replayed an occlusion event');
          }
          continue;
        } else {
          const expected = phase === 'partial' ? 1 : phase.includes('full') || phase === 'hidden' ? 2 : 0;
          await this.wait(expected);
          const aggregate = expected === 2 ? 2 : 1;
          this.result = await nativeTests.occlusionCheck(aggregate, false, this.target.isWindowShowing());
          this.result = await nativeTests.occlusionCheck(aggregate, true, this.target.isWindowShowing());
          const after = this.state();
          if (phase.startsWith('unobserved') && after.objects !== before.objects) { throw new Error('Removed observer received an event'); }
          if ((phase === 'partial' || phase === 'uncovered' && i > 0) && before.state === aggregate && after.objects !== before.objects) {
            throw new Error('Aggregate Visible transition was duplicated');
          }
          this.awaiting = true;
          console.info(`NATIVEAPI_OCCLUSION_PHASE ${i} ${phase} sdk=${this.sdk} native=${this.result} target=${JSON.stringify(this.target.getWindowProperties())} cover=${JSON.stringify(this.cover.getWindowProperties())}`);
          for (let j = 0; j < 400 && this.awaiting; ++j) {
            await new Promise<void>((resolve) => { setTimeout(resolve, 100); });
          }
          if (this.awaiting) { throw new Error('Scene verification acknowledgement timed out'); }
        }
        if (this.state().failed !== 0) { throw new Error('Native occlusion assertions failed'); }
      }
      nativeTests.occlusionCommand('finish');
      console.info('NATIVEAPI_OCCLUSION_UI_COMPLETE'); this.result = 'NativeAPI occlusion acceptance complete';
    } catch (error) { this.result = `NATIVEAPI_OCCLUSION_ERROR ${error}`; console.error(this.result); }
    this.busy = false;
  }
  build() {
    Column({ space: 12 }) {
      Text('NativeAPI Occlusion Acceptance').fontSize(22)
      Blank()
      Text(this.result).fontSize(14)
      Button('NativeAPI Verify Next Scene').id('nativeapi-occlusion-next').enabled(this.awaiting)
        .onClick(() => { this.awaiting = false; })
      Button('NativeAPI Run Occlusion').id('nativeapi-occlusion-run').enabled(this.ready && !this.busy)
        .onClick(() => { this.run(); })
    }.padding(24).width('100%').height('100%')
  }
}
