import { window } from '@kit.ArkUI';
import { common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import nativeTests from 'libnativeapi_smoke.so';
interface NativeControls { ready: boolean; alive: boolean; visible: boolean; objects: number; managers: number; cEvents: number; passed: number; failed: number; }
@Entry
@Component
struct WindowControls {
  @State result: string='Preparing window controls';
  @State ready: boolean=false;
  @State busy: boolean=false;
  @State awaiting: boolean=false;
  private main?: window.Window;
  private child?: window.Window;
  private available: boolean=false;
  private state(command: string=''): NativeControls { return JSON.parse(nativeTests.controlsState(command)) as NativeControls; }
  private sdkVisible(): boolean {
    if(!this.main) { return false; }
    try { const r=this.main.getTitleButtonRect();return r.width>0 && r.height>0; }
    catch(error) { return false; }
  }
  aboutToAppear(): void {
    setTimeout(async () => {
      try {
        this.main=await window.getLastWindow(this.getUIContext().getHostContext() as common.UIAbilityContext);
        let code=0;
        try { this.main.setWindowTitleButtonVisible(true,true,true); } catch(error) { code=(error as BusinessError).code; }
        console.info(`NATIVEAPI_CONTROLS_MAIN_SDK_CODE ${code}`);
        if(code!==0 && code!==801 && code!==1300004) { throw new Error('Unexpected main SDK control result'); }
        this.available=code===0;
        await new Promise<void>((resolve) => { setTimeout(resolve,350); });
        nativeTests.controlsState('setup');
        for(let j=0;j<100 && !this.state().ready;++j) { await new Promise<void>((resolve) => { setTimeout(resolve,100); }); }
        if(!this.state().ready) { throw new Error('Child did not become ready'); }
        this.child=window.findWindow('NativeAPI Controls Child');
        let childCode=0;
        try { this.child.setWindowTitleButtonVisible(true,true,true); } catch(error) { childCode=(error as BusinessError).code; }
        console.info(`NATIVEAPI_CONTROLS_CHILD_SDK_CODE ${childCode}`);
        if(childCode!==801 && childCode!==1300004) { throw new Error('Unexpected ordinary child SDK result'); }
        this.ready=true;
        console.info(`NATIVEAPI_CONTROLS_READY available=${this.available} main=${JSON.stringify(this.main.getWindowProperties())} child=${JSON.stringify(this.child.getWindowProperties())}`);
      } catch(error) { console.error(`NATIVEAPI_CONTROLS_ERROR setup ${error}`); }
    },500);
  }
  private async run(): Promise<void> {
    if(!this.main || !this.child || !this.ready || this.busy) { return; }this.busy=true;
    try {
      await new Promise<void>((resolve) => { setTimeout(resolve,300); });
      const baseline=this.state().objects;
      const phases=['baseline','hide','hide-again','show','sdk-hide','sdk-close-only','sdk-max-only','hide-worker','show-ui','hide-decor','show-hidden','show-decor','unlisten-hide','listen-show','child','hidden-maximize','hidden-recover','restore'];
      const visible=[true,false,false,true,false,true,true,false,true,false,true,true,false,true,true,false,false,true];
      const events=[0,1,1,2,3,4,4,5,6,7,8,8,8,9,9,10,10,11];
      for(let i=0;i<phases.length;++i) {
        const phase=phases[i];
        if(phase==='unlisten-hide') { nativeTests.controlsState('unlisten'); }
        if(phase==='listen-show') { nativeTests.controlsState('listen'); }
        if(phase==='sdk-hide' || phase==='sdk-close-only' || phase==='sdk-max-only' || phase==='unlisten-hide') {
          if(this.available) { this.main.setWindowTitleButtonVisible(phase==='sdk-max-only',false,phase==='sdk-close-only'); }
        } else if(phase==='hidden-maximize') {
          await nativeTests.controlsAction('hide',false,true);
          if(this.available) { await nativeTests.controlsAction('maximize',false,false); }
        } else if(phase==='hidden-recover') {
          if(this.available) { await nativeTests.controlsAction('recover',true,false); }
        } else if(phase!=='baseline') {
          const command=phase==='hide-again' || phase==='hide-worker' ? 'hide' : phase==='show-ui' || phase==='restore' || phase==='listen-show' || phase==='show-hidden' ? 'show' : phase;
          await nativeTests.controlsAction(command,i===2 || i===3 || i===10 || i===13 || i===17,i===2 || i===3 || i===7 || i===17);
        }
        const expected=this.available && visible[i];const count=baseline+(this.available ? events[i] : 0);
        for(let j=0;j<100;++j) {
          const current=this.state();
          const status=this.main.getWindowStatus();
          const geometryReady=!this.available || phase!=='hidden-maximize' && phase!=='hidden-recover' || status===(phase==='hidden-maximize' ? 2 : 4);
          if(current.visible===expected && this.sdkVisible()===expected && current.objects===count && current.managers===count && current.cEvents===count && geometryReady) { break; }
          await new Promise<void>((resolve) => { setTimeout(resolve,100); });
        }
        const current=this.state();
        if(current.failed!==0 || current.visible!==expected || this.sdkVisible()!==expected || current.objects!==count || current.managers!==count || current.cEvents!==count) { throw new Error(`Control state/event mismatch ${i} ${JSON.stringify(current)} expected=${expected}/${count}`); }
        this.result=nativeTests.controlsState('verify');
        await new Promise<void>((resolve) => { setTimeout(resolve,250); });
        this.awaiting=true;
        console.info(`NATIVEAPI_CONTROLS_PHASE ${i} ${phase} native=${this.result} target=${JSON.stringify(this.main.getWindowProperties())}`);
        for(let j=0;j<400 && this.awaiting;++j) { await new Promise<void>((resolve) => { setTimeout(resolve,100); }); }
        if(this.awaiting) { throw new Error('Control acknowledgement timed out'); }
      }
      await nativeTests.controlsAction('close',false,false);
      for(let j=0;j<100 && this.state().alive;++j) { await new Promise<void>((resolve) => { setTimeout(resolve,100); }); }
      this.result=nativeTests.controlsState('finish');console.info('NATIVEAPI_CONTROLS_UI_COMPLETE');
    } catch(error) { this.result=`NATIVEAPI_CONTROLS_ERROR ${error}`;console.error(this.result); }
    this.busy=false;
  }
  build() {
    Column({space:12}) {
      Text('NativeAPI Window Controls Acceptance').fontSize(22)
      Blank()
      Text(this.result).fontSize(12)
      Button('NativeAPI Next Controls').id('nativeapi-controls-next').enabled(this.awaiting).onClick(() => { this.awaiting=false; })
      Button('NativeAPI Run Controls').id('nativeapi-controls-run').enabled(this.ready && !this.busy).onClick(() => { this.run(); })
    }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF')
  }
}
