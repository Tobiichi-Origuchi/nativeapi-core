import { window } from '@kit.ArkUI';
import { common } from '@kit.AbilityKit';
import { image } from '@kit.ImageKit';
import nativeTests from 'libnativeapi_smoke.so';
import { WindowShadowFixture } from './WindowShadowFixture';
interface ProbeImage { pixels: image.PixelMap; margin: number; width: number; height: number; }
@Entry
@Component
struct WindowShadowProbe {
  @StorageLink('nativeapiShadowAction') @Watch('command') action: string = '';
  @State result: string = 'Preparing shadow helper feasibility test';
  private awaiting: boolean = false;
  private command(): void { if(this.action) this.awaiting=false; }
  private async pause(): Promise<void> {await new Promise<void>((resolve)=>{setTimeout(resolve,300);});}
  private async run(): Promise<void> {
    try {
      const stage=WindowShadowFixture.stage;if(!stage)throw new Error('Missing stage');
      const main=await window.getLastWindow(this.getUIContext().getHostContext() as common.UIAbilityContext);
      const parent=main.getWindowProperties().windowRect;
      const target=await stage.createSubWindow('NativeAPI Shadow Probe Target');
      await target.loadContent('pages/ShadowProbeTarget');
      const width=Math.min(540,parent.width-300),height=360;
      await target.resize(width,height);await target.moveWindowTo(parent.left+150,parent.top+220);
      target.setWindowShadowRadius(0);await target.setWindowCornerRadius(0);await target.showWindow();
      const density=target.getWindowDensityInfo().systemDensity;
      const helper=await stage.createSubWindow('NativeAPI Shadow Probe Helper');
      const storage=new LocalStorage();
      await helper.loadContent('pages/ShadowProbeSurface',storage);
      helper.setWindowBackgroundColor('#00000000');helper.setWindowShadowRadius(0);await helper.setWindowCornerRadius(0);
      await helper.setWindowFocusable(false);await helper.setWindowTouchable(false);await helper.setSubWindowZLevel(-1);
      const phases=['baseline','soft-on','off','hard-offset','moved','hidden','reshown','destroyed'];
      let data: ProbeImage|undefined;
      for(let i=0;i<phases.length;++i) {
        const name=phases[i];
        const focused=target.isFocused();
        if(name==='soft-on'||name==='hard-offset') {
          const rect=target.getWindowProperties().windowRect;
          data=nativeTests.shadowProbeImage(rect.width,rect.height,density,name==='soft-on'?18:0,12,8);
          storage.setOrCreate('pixels',data.pixels);
          await helper.resize(data.width,data.height);await helper.moveWindowTo(rect.left-data.margin,rect.top-data.margin);
          await helper.showWindow({focusOnShow:false});
        } else if(name==='off'||name==='hidden') {await helper.minimize();if(name==='hidden')await target.minimize();}
        else if(name==='moved'&&data) {
          const r=target.getWindowProperties().windowRect;await target.moveWindowTo(r.left+60,r.top+80);
          await this.pause();
          const current=target.getWindowProperties().windowRect;await helper.moveWindowTo(current.left-data.margin,current.top-data.margin);
        } else if(name==='reshown') {await target.showWindow({focusOnShow:false});await helper.showWindow({focusOnShow:false});}
        else if(name==='destroyed') {await target.destroyWindow();await helper.destroyWindow();}
        await this.pause();this.awaiting=true;
        const state=JSON.stringify({main:main.getWindowProperties(),target:name==='destroyed'?null:target.getWindowProperties(),helper:name==='destroyed'?null:helper.getWindowProperties(),
          density:density,margin:data?data.margin:0,focus:name==='hidden'||name==='destroyed'?false:target.isFocused(),initialFocus:focused});
        this.result=`${name}: ${state}`;console.info(`NATIVEAPI_SHADOW_PHASE ${i} ${name} ${state}`);
        for(let j=0;j<120&&this.awaiting;++j)await this.pause();
        if(this.awaiting)throw new Error('Scene acknowledgement timeout');
      }
      console.info('NATIVEAPI_SHADOW_DONE');this.result='Shadow helper feasibility passed';
    } catch(error) {console.error(`NATIVEAPI_SHADOW_ERROR ${error.code} ${error.message}`);}
  }
  aboutToAppear(): void {setTimeout(()=>{this.run();},400);}
  build() { Column() { Text('NativeAPI shadow helper feasibility').fontSize(22);Blank();Text(this.result).fontSize(12) }
    .width('100%').height('100%').backgroundColor('#FFFFFF')
    .onTouch((event: TouchEvent)=>{if(event.type===TouchType.Down)console.info('NATIVEAPI_SHADOW_TOUCH main');}) }
}
