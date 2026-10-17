import { window } from '@kit.ArkUI';
import { bundleManager } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
export async function runControlProbe(stage: window.WindowStage): Promise<void> {
  const main=stage.getMainWindowSync();
  const modes=[bundleManager.SupportWindowMode.FULL_SCREEN,bundleManager.SupportWindowMode.SPLIT,bundleManager.SupportWindowMode.FLOATING];
  const phases=['baseline','hide-all','show-all','hide-max','maximize-hidden','recover-hidden','float-only','maximize-float-only','recover-float-only','fullscreen-float-only','restore-modes','recover-restored','maximize-restored','recover-final','restore-buttons'];
  try {
    for(let i=0;i<phases.length;++i) {
      const phase=phases[i];let code=0;
      try {
        if(phase==='hide-all') { main.setWindowTitleButtonVisible(false,false,false); }
        if(phase==='show-all' || phase==='restore-buttons') { main.setWindowTitleButtonVisible(true,true,true); }
        if(phase==='hide-max') { main.setWindowTitleButtonVisible(false,true,true); }
        if(phase==='maximize-hidden' || phase==='maximize-float-only') { await main.maximize(window.MaximizePresentation.EXIT_IMMERSIVE); }
        if(phase==='recover-hidden' || phase==='recover-float-only') { await main.recover(); }
        if(phase==='float-only') { await stage.setSupportedWindowModes([bundleManager.SupportWindowMode.FLOATING],true); }
        if(phase==='fullscreen-float-only') { await main.maximize(window.MaximizePresentation.ENTER_IMMERSIVE); }
        if(phase==='restore-modes') { await stage.setSupportedWindowModes(modes,true); }
        if(phase==='recover-restored' || phase==='recover-final') { await main.recover(); }
        if(phase==='maximize-restored') { await main.maximize(window.MaximizePresentation.EXIT_IMMERSIVE); }
      } catch(error) { code=(error as BusinessError).code; }
      await new Promise<void>((resolve) => { setTimeout(resolve,450); });
      let rect: window.TitleButtonRect={right:0,top:0,width:0,height:0};let rectCode=0;
      try { rect=main.getTitleButtonRect(); } catch(error) { rectCode=(error as BusinessError).code; }
      let decor=false;let decorCode=0;
      try { decor=main.getWindowDecorVisible(); } catch(error) { decorCode=(error as BusinessError).code; }
      const result=`modes=${JSON.stringify(modes)} code=${code} rectCode=${rectCode} decorCode=${decorCode} decor=${decor} rect=${JSON.stringify(rect)} status=${main.getWindowStatus()} target=${JSON.stringify(main.getWindowProperties())}`;
      AppStorage.setOrCreate('nativeapiControlProbeResult',phase+' '+result);
      AppStorage.setOrCreate('nativeapiControlProbeNext',false);
      AppStorage.setOrCreate('nativeapiControlProbeAwaiting',true);
      console.info(`NATIVEAPI_CONTROL_PROBE_PHASE ${i} ${phase} ${result}`);
      for(let j=0;j<400 && !AppStorage.get<boolean>('nativeapiControlProbeNext');++j) { await new Promise<void>((resolve) => { setTimeout(resolve,100); }); }
      if(!AppStorage.get<boolean>('nativeapiControlProbeNext')) { throw new Error('Probe acknowledgement timeout'); }
      AppStorage.set('nativeapiControlProbeAwaiting',false);
    }
    console.info('NATIVEAPI_CONTROL_PROBE_COMPLETE');
  } catch(error) { console.error(`NATIVEAPI_CONTROL_ERROR ${error}`); }
}
@Entry
@Component
struct ControlProbe {
  @StorageLink('nativeapiControlProbeResult') result: string='Preparing control probe';
  @StorageLink('nativeapiControlProbeNext') next: boolean=false;
  @StorageLink('nativeapiControlProbeAwaiting') awaiting: boolean=false;
  build() {
    Column({space:16}) {
      Text('NativeAPI SDK Window Controls Probe').fontSize(22)
      Blank()
      Text(this.result).fontSize(12)
      Button('NativeAPI Next Control Probe').id('nativeapi-control-probe-next').enabled(this.awaiting).onClick(() => { this.next=true; })
    }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF')
  }
}
