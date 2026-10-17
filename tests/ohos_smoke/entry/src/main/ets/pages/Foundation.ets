import { window, display } from '@kit.ArkUI';
import nativeTests from 'libnativeapi_smoke.so';
interface ValueReply { passed: number; failed: number; }
interface WindowReply { ready: boolean; passed: number; failed: number; cpp: number[]; copy: number[]; c: number[]; offset: number[]; }
@Entry
@Component
struct FoundationTest {
  @State result: string = 'Running foundation API acceptance';
  private async pause(): Promise<void> { await new Promise<void>((resolve) => { setTimeout(resolve, 200); }); }
  private state(action: string = ''): WindowReply { return JSON.parse(nativeTests.foundationWindow(action)) as WindowReply; }
  private phase(name: string, child: window.Window): void {
    const p = child.getWindowProperties();
    const scale = display.getDisplayByIdSync(p.displayId).densityPixels;
    const d = p.drawableRect, w = p.windowRect;
    const sdk = [(d.left + w.left) / scale, (d.top + w.top) / scale, d.width / scale, d.height / scale];
    const reply = this.state();
    for (const rect of [reply.cpp, reply.copy, reply.c]) {
      if (!rect || rect.some((value: number, index: number) => Math.abs(value - sdk[index]) > 0.002)) {
        throw new Error(`SDK/native geometry differs ${name} ${JSON.stringify({sdk:sdk,native:reply})}`);
      }
    }
    if (reply.failed || JSON.stringify(reply.offset) !== '[3.25,-4.5,3.25,-4.5]') throw new Error(`Window strategy failed ${JSON.stringify(reply)}`);
    console.info(`NATIVEAPI_FOUNDATION_PHASE ${name} ${JSON.stringify({sdk:sdk,native:reply})}`);
  }
  private async run(): Promise<void> {
    try {
      const ui = JSON.parse(await nativeTests.foundationValues(false)) as ValueReply;
      const worker = JSON.parse(await nativeTests.foundationValues(true)) as ValueReply;
      if (ui.failed || worker.failed || ui.passed !== worker.passed) throw new Error(`Value checks failed ${JSON.stringify({ui:ui,worker:worker})}`);
      this.state('setup');
      for(let i=0;i<50 && !this.state().ready;++i) await this.pause();
      if(!this.state().ready) throw new Error('Real target did not attach');
      const child=window.findWindow('NativeAPI Foundation Target');
      await child.resize(540,420); await child.moveWindowTo(180,300); await this.pause();
      this.phase('sdk-first-geometry',child);
      await child.resize(630,480); await child.moveWindowTo(270,450); await this.pause();
      this.phase('sdk-moved-resized',child);
      await child.destroyWindow(); await this.pause();
      const released=this.state('release');
      if(released.failed || released.ready) throw new Error(`Window ownership checks failed ${JSON.stringify(released)}`);
      const done=JSON.stringify({ui:ui,worker:worker,window:released,phases:2,failed:0});
      this.result=done;
      console.info(`NATIVEAPI_FOUNDATION_DONE ${done}`);
    } catch(error) { console.error(`NATIVEAPI_FOUNDATION_ERROR ${error.code} ${error.message}`); }
  }
  aboutToAppear(): void { setTimeout(()=>{this.run();},400); }
  build() { Column({space:20}) { Text('NativeAPI Foundation Acceptance').fontSize(22)
    Text(this.result).fontSize(14)
  }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF') }
}
