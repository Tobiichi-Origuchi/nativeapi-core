import { window } from '@kit.ArkUI';
import { common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import nativeTests from 'libnativeapi_smoke.so';
interface NativeDrag { ready: boolean; alive: boolean; mainMoves: number; childMoves: number; passed: number; failed: number; }
@Entry
@Component
struct WindowDrag {
  @State result: string = 'Preparing native window drag';
  @State ready: boolean = false;
  @State busy: boolean = false;
  @State awaiting: boolean = false;
  private main?: window.Window;
  private child?: window.Window;
  private state(command: string = ''): NativeDrag { return JSON.parse(nativeTests.dragState(command)) as NativeDrag; }
  private async inputAudit(target: window.Window, label: string): Promise<void> {
    let code = 0;
    try { await target.setTouchableAreas([{left:0,top:0,width:20,height:20}]); }
    catch(error) { code=(error as BusinessError).code; }
    console.info(`NATIVEAPI_DRAG_INPUT_PERMISSION ${label} code=${code}`);
    if(code!==201 && code!==801) { throw new Error(`Unexpected restricted input-area result ${code}`); }
  }
  aboutToAppear(): void {
    setTimeout(async () => {
      try {
        this.main = await window.getLastWindow(this.getUIContext().getHostContext() as common.UIAbilityContext);
        await this.inputAudit(this.main,'main');
        nativeTests.dragState('setup');
        for(let j=0;j<100 && !this.state().ready;++j) { await new Promise<void>((resolve) => { setTimeout(resolve,100); }); }
        if(!this.state().ready) { throw new Error('Drag target not ready'); }
        this.child=window.findWindow('NativeAPI Drag Target');
        await this.inputAudit(this.child,'child');
        this.ready=true;
        console.info(`NATIVEAPI_DRAG_READY main=${JSON.stringify(this.main.getWindowProperties())} child=${JSON.stringify(this.child.getWindowProperties())}`);
      } catch(error) { console.error(`NATIVEAPI_DRAG_ERROR setup ${error}`); }
    },500);
  }
  private async run(): Promise<void> {
    if(!this.main || !this.child || !this.ready || this.busy) { return; } this.busy=true;
    try {
      const phases=['child-cpp','child-c','child-sdk','child-off','main-cpp','main-c','main-sdk','main-off'];
      for(let i=0;i<phases.length;++i) {
        if(i===4) {
          nativeTests.dragState('hide-child');
          for(let j=0;j<100 && this.child.isWindowShowing();++j) { await new Promise<void>((resolve) => { setTimeout(resolve,100); }); }
          if(this.child.isWindowShowing()) { throw new Error('Child did not hide'); }
        }
        const phase=phases[i]; const target=i<4 ? this.child : this.main;
        AppStorage.setOrCreate('nativeapiDragMode',phase.substring(phase.indexOf('-')+1));
        await new Promise<void>((resolve) => { setTimeout(resolve,250); });
        this.awaiting=true;
        console.info(`NATIVEAPI_DRAG_PHASE ${i} ${phase} native=${JSON.stringify(this.state())} target=${JSON.stringify(target.getWindowProperties())}`);
        for(let j=0;j<400 && this.awaiting;++j) { await new Promise<void>((resolve) => { setTimeout(resolve,100); }); }
        if(this.awaiting) { throw new Error('Drag acknowledgement timeout'); }
        this.result=nativeTests.dragState(i<4 ? 'verify-child' : 'verify-main');
        if(this.state().failed!==0) { throw new Error('Native drag assertion failed'); }
      }
      nativeTests.dragState('close');
      for(let j=0;j<100 && this.state().alive;++j) { await new Promise<void>((resolve) => { setTimeout(resolve,100); }); }
      this.result=nativeTests.dragState('finish'); console.info('NATIVEAPI_DRAG_UI_COMPLETE');
    } catch(error) { this.result=`NATIVEAPI_DRAG_ERROR ${error}`; console.error(this.result); }
    this.busy=false;
  }
  build() {
    Column({space:12}) {
      Text('NativeAPI Window Drag Acceptance').fontSize(22)
      Text('NativeAPI Main Drag Pad').id('nativeapi-main-drag-pad').width('90%').height(80).fontSize(18)
        .textAlign(TextAlign.Center).backgroundColor('#DDEEFF')
        .onTouch((event: TouchEvent) => {
          if(event.type!==TouchType.Down) { return; }
          const mode=AppStorage.get<string>('nativeapiDragMode') ?? 'off';
          console.info(`NATIVEAPI_DRAG_DOWN main ${mode}`);
          if(mode==='cpp' || mode==='c') { nativeTests.dragStart(true,mode==='c'); }
          if(mode==='sdk' && this.main) {
            this.main.startMoving().then(() => { console.info('NATIVEAPI_DRAG_REFERENCE main code=0'); })
              .catch((error: BusinessError) => { console.info(`NATIVEAPI_DRAG_REFERENCE main code=${error.code}`); });
          }
        })
      Blank()
      Text(this.result).fontSize(12)
      Button('NativeAPI Next Drag').id('nativeapi-drag-next').enabled(this.awaiting).onClick(() => { this.awaiting=false; })
      Button('NativeAPI Run Drag').id('nativeapi-drag-run').enabled(this.ready && !this.busy).onClick(() => { this.run(); })
    }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF')
  }
}
