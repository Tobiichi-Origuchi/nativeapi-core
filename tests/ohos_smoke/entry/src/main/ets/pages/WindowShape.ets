import { window } from '@kit.ArkUI';
import { common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import nativeTests from 'libnativeapi_smoke.so';
interface NativeShape { ready: boolean; alive: boolean; shaped: boolean; passed: number; failed: number; }
@Entry
@Component
struct WindowShape {
  @State result: string = 'Preparing native window shape';
  @State ready: boolean = false;
  @State busy: boolean = false;
  @State awaiting: boolean = false;
  private target?: window.Window;
  private density: number = 1;
  private state(command: string = ''): NativeShape { return JSON.parse(nativeTests.shapeState(command)) as NativeShape; }
  aboutToAppear(): void {
    setTimeout(async () => {
      try {
        const main = await window.getLastWindow(this.getUIContext().getHostContext() as common.UIAbilityContext);
        let code = 0;
        try { const r = main.getWindowProperties().windowRect; await main.setWindowMaskWithAlpha(new Uint8Array(r.width*r.height).fill(255),r.width,r.height); } catch (error) { code = (error as BusinessError).code; }
        console.info(`NATIVEAPI_SHAPE_MAIN_SDK_CODE ${code}`);
        if (code !== 1300004 && code !== 801) { throw new Error(`Unexpected main mask result ${code}`); }
        nativeTests.shapeState('setup');
        for (let i=0;i<100 && !this.state().ready;++i) { await new Promise<void>((resolve) => { setTimeout(resolve,100); }); }
        if (!this.state().ready) { throw new Error('Missing native shape child'); }
        this.target = window.findWindow('NativeAPI Window Shape');
        this.density = this.target.getWindowDensityInfo().systemDensity;
        console.info(`NATIVEAPI_SHAPE_READY density=${this.density} target=${JSON.stringify(this.target.getWindowProperties())}`);
        this.ready = true;
      } catch (error) { console.error(`NATIVEAPI_SHAPE_ERROR setup ${error}`); }
    },500);
  }
  private async run(): Promise<void> {
    if (!this.target || !this.ready || this.busy) { return; } this.busy = true;
    try {
      const phases = ['empty-clear','pending','clear','concave','clear','bow','outside','rectangle','clear','resize-triangle','clear','reference','clear'];
      for (let i=0;i<phases.length;++i) {
        const phase = phases[i];
        if (phase === 'resize-triangle') {
          await this.target.resize(Math.round(200*this.density),Math.round(140*this.density));
          await nativeTests.shapeAction('resize-probe',false,true);
          console.info(`NATIVEAPI_SHAPE_RESIZE_FIRST ${JSON.stringify(this.state())} sdk=${JSON.stringify(this.target.getWindowProperties())}`);
          for (let j=0;j<100;++j) {
            const rect = this.target.getWindowProperties().windowRect;
            if (rect.width===Math.round(200*this.density) && rect.height===Math.round(140*this.density)) { break; }
            await new Promise<void>((resolve) => { setTimeout(resolve,50); });
          }
        }
        if (phase === 'reference') {
          const rect = this.target.getWindowProperties().windowRect;
          const mask: Array<Array<number>> = [];
          for (let y=0;y<rect.height;++y) {
            const row: number[] = [];
            for (let x=0;x<rect.width;++x) { row.push((x+.5)/rect.width < .5 ? 1 : 0); }
            mask.push(row);
          }
          await this.target.setWindowMask(mask);
        } else if (i === phases.length-1) {
          await this.target.clearWindowMask();
        } else {
          await nativeTests.shapeAction(phase, i===2 || i===3 || i===6 || i===7 || i===10, i!==1 && i!==4);
        }
        const expected = phase !== 'clear' && phase !== 'empty-clear' && phase !== 'reference';
        for (let j=0;j<100 && this.state().shaped!==expected;++j) { await new Promise<void>((resolve) => { setTimeout(resolve,100); }); }
        if (this.state().shaped!==expected || this.state().failed!==0) { throw new Error('Native mask failed to settle'); }
        this.result = nativeTests.shapeState('verify');
        await new Promise<void>((resolve) => { setTimeout(resolve,350); });
        this.awaiting = true;
        console.info(`NATIVEAPI_SHAPE_PHASE ${i} ${phase} native=${this.result} target=${JSON.stringify(this.target.getWindowProperties())}`);
        for(let j=0;j<400 && this.awaiting;++j) { await new Promise<void>((resolve) => { setTimeout(resolve,100); }); }
        if(this.awaiting) { throw new Error('Scene acknowledgement timeout'); }
      }
      await nativeTests.shapeAction('close',false,false);
      for(let j=0;j<100 && this.state().alive;++j) { await new Promise<void>((resolve) => { setTimeout(resolve,100); }); }
      this.result = nativeTests.shapeState('finish'); console.info('NATIVEAPI_SHAPE_UI_COMPLETE');
    } catch(error) { this.result = `NATIVEAPI_SHAPE_ERROR ${error}`; console.error(this.result); }
    this.busy=false;
  }
  build() {
    Column({space:12}) {
      Text('NativeAPI Window Shape Acceptance').fontSize(22)
      Blank()
      Text(this.result).fontSize(12)
      Button('NativeAPI Next Window Shape').id('nativeapi-shape-next').enabled(this.awaiting).onClick(() => { this.awaiting=false; })
      Button('NativeAPI Run Window Shape').id('nativeapi-shape-run').enabled(this.ready && !this.busy).onClick(() => { this.run(); })
    }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF')
      .onTouch((event: TouchEvent) => {
        if(event.type===TouchType.Down) { console.info('NATIVEAPI_SHAPE_TOUCH main'); }
      })
  }
}
