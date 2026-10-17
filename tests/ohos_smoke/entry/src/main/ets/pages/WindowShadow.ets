import { window } from '@kit.ArkUI';
import { common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import nativeTests from 'libnativeapi_smoke.so';
import { WindowShadowFixture } from './WindowShadowFixture';
interface ShadowState {passed: number;failed: number;configured: boolean;enabled: boolean;alive: boolean;color: number;blur: number;offset: number[];}
interface ShadowHelper {properties: window.WindowProperties;showing: boolean;}
@Entry
@Component
struct WindowShadowTest {
  @StorageLink('nativeapiShadowAction') @Watch('command') action: string = '';
  @State result: string = 'Preparing actual custom shadow acceptance';
  private awaiting: boolean = false;
  private command(): void {if(this.action)this.awaiting=false;}
  private async pause(): Promise<void> {await new Promise<void>((resolve)=>{setTimeout(resolve,200);});}
  private state(action: string = ''): ShadowState {return JSON.parse(nativeTests.shadowState(action)) as ShadowState;}
  private helpers(id: number): ShadowHelper[] {
    const values: ShadowHelper[]=[];
    for(let i=1;i<=10;++i)try {const helper=window.findWindow(`NativeAPI shadow ${id} ${i}`);values.push({properties:helper.getWindowProperties(),showing:helper.isWindowShowing()});}catch(_){}
    return values;
  }
  private async run(): Promise<void> {
    try {
      const stage=WindowShadowFixture.stage;if(!stage)throw new Error('Missing stage');
      const main=await window.getLastWindow(this.getUIContext().getHostContext() as common.UIAbilityContext);
      const parent=main.getWindowProperties().windowRect;
      const target=await stage.createSubWindow('NativeAPI Custom Shadow Target');
      await target.loadContent('pages/ShadowProbeTarget');
      target.on('windowRectChange',(event: window.RectChangeOptions)=>{console.info(`NATIVEAPI_SHADOW_RECT ${JSON.stringify(event)}`);});
      target.on('windowSizeChange',(size: window.Size)=>{console.info(`NATIVEAPI_SHADOW_SIZE ${JSON.stringify(size)}`);});
      const width=Math.min(540,parent.width-300),height=360;
      await target.resize(width,height);await target.moveWindowTo(parent.left+150,parent.top+220);
      target.setWindowShadowRadius(0);await target.setWindowCornerRadius(0);await target.showWindow();await this.pause();
      let captionQuery=0,captionEnable=0;
      try {target.getWindowDecorVisible();}catch(error){captionQuery=(error as BusinessError).code;}
      if(captionQuery===801) {
        try {await target.setWindowDecorVisible(true);}catch(error){captionEnable=(error as BusinessError).code;}
        if(captionEnable===0)await target.setWindowDecorVisible(false);
        else if(captionEnable!==801)throw new Error(`Unexpected caption SDK result ${captionQuery}/${captionEnable}`);
      }
      console.info(`NATIVEAPI_SHADOW_CAPTION_REFERENCE ${JSON.stringify({query:captionQuery,enable:captionEnable,properties:target.getWindowProperties()})}`);
      nativeTests.shadowTargetAttach(target);
      const id=target.getWindowProperties().id,density=target.getWindowDensityInfo().systemDensity;
      const phases=['baseline','disabled-config','enable-soft','disable-soft','enable-soft-c','hard-red-c','hard-green-ui','helper-recreated','queued-off','queued-on','triangle','outside','clear-shape','moved','resized','hidden','reshown','clear','default-off','default-on','closed'];
      let color=0,blur=0,enabled=false,configured=false;
      let expectedWidth=width,expectedHeight=height,expectedLeft=parent.left+150,expectedTop=parent.top+220;
      for(let i=0;i<phases.length;++i) {
        const name=phases[i],focused=name==='closed'?false:target.isFocused();
        let ok=true;
        if(name==='disabled-config'){ok=await nativeTests.shadowAction('soft',false,true);configured=true;color=0xff0000c0;blur=18;}
        else if(name==='enable-soft'||name==='enable-soft-c'){ok=await nativeTests.shadowAction('enable',name==='enable-soft-c',true);enabled=true;}
        else if(name==='disable-soft'||name==='default-off'){ok=await nativeTests.shadowAction('disable',true,true);enabled=false;}
        else if(name==='hard-red-c'){ok=await nativeTests.shadowAction('hard',true,true);color=0xff0000c0;blur=0;}
        else if(name==='hard-green-ui'){ok=await nativeTests.shadowAction('green',false,false);color=0x00ff00c0;blur=0;}
        else if(name==='helper-recreated') {
          const active=this.helpers(id).filter((h: ShadowHelper)=>h.showing);
          if(active.length!==1)throw new Error('Missing helper before SDK destruction');
          await window.findWindow(active[0].properties.name).destroyWindow();
          ok=await nativeTests.shadowAction('green',true,true);
        }
        else if(name==='queued-off') {
          ok=await nativeTests.shadowAction('soft',false,false);
          ok=await nativeTests.shadowAction('disable',true,false)&&ok;
          color=0xff0000c0;blur=18;enabled=false;
        } else if(name==='queued-on') {
          ok=await nativeTests.shadowAction('hard',false,false);
          ok=await nativeTests.shadowAction('enable',false,false)&&ok;
          color=0xff0000c0;blur=0;enabled=true;
        } else if(name==='triangle'||name==='outside'||name==='clear-shape') {
          ok=await nativeTests.shadowAction(name==='triangle'?'shape':name,false,true);
        }
        else if(name==='moved'){expectedLeft+=60;expectedTop+=80;await target.moveWindowTo(expectedLeft,expectedTop);}
        else if(name==='resized'){expectedWidth=width+80;expectedHeight=height+60;await target.resize(expectedWidth,expectedHeight);}
        else if(name==='hidden')await target.minimize();
        else if(name==='reshown')await target.showWindow({focusOnShow:false});
        else if(name==='clear'){ok=await nativeTests.shadowAction('clear',true,true);configured=false;color=0;blur=0;}
        else if(name==='default-on'){ok=await nativeTests.shadowAction('enable',false,true);enabled=true;}
        else if(name==='closed'){await target.destroyWindow();configured=false;enabled=false;}
        if(!ok)throw new Error(`Native shadow request refused ${name}`);
        for(let j=0;j<100;++j) {
          const state=this.state();
          if(state.configured===configured&&state.enabled===enabled&&state.color===color&&state.blur===blur)break;
          if(j===99)throw new Error(`Native shadow did not settle ${name} ${JSON.stringify(state)}`);
          await this.pause();
        }
        // SDK promises may resolve before compositor/window-property snapshots.
        // Wait for actual helper geometry/visibility, not an arbitrary fixed delay.
        for(let j=0;j<60;++j) {
          const observed=this.helpers(id),visible=observed.filter((h: ShadowHelper)=>h.showing);
          let settled=false;
          if(name==='closed')settled=observed.length===0;
          else if(configured&&enabled&&name!=='hidden') {
            const r=target.getWindowProperties().windowRect;
            const margin=Math.ceil(blur*density/3)*3+Math.ceil(12*density)+2;
            settled=r.left===expectedLeft&&r.top===expectedTop&&r.width===expectedWidth&&r.height===expectedHeight&&visible.length===1&&visible[0].properties.windowRect.left===r.left-margin&&visible[0].properties.windowRect.top===r.top-margin&&
              visible[0].properties.windowRect.width===r.width+margin*2&&visible[0].properties.windowRect.height===r.height+margin*2;
          } else settled=visible.length===0;
          if(settled)break;
          if(j===59)throw new Error(`Actual shadow geometry never settled ${name} ${JSON.stringify(observed)}`);
          await this.pause();
        }
        await this.pause();
        const state=this.state('verify');if(state.failed)throw new Error(`Native checks failed ${JSON.stringify(state)}`);
        const helpers=this.helpers(id);
        if(name!=='closed') {
          if(!['hidden','reshown','triangle','outside','clear-shape'].includes(name)&&target.isFocused()!==focused)throw new Error(`Shadow stole focus ${name}`);
          const visible=helpers.filter((h: ShadowHelper)=>h.showing);
          if(configured&&enabled&&name!=='hidden') {
            if(visible.length!==1)throw new Error(`Missing unique shadow helper ${name} ${JSON.stringify(helpers)}`);
            const p=visible[0].properties,r=target.getWindowProperties().windowRect;
            const margin=Math.ceil(blur*density/3)*3+Math.ceil(12*density)+2;
            if(p.touchable||p.focusable||p.windowRect.left!==r.left-margin||p.windowRect.top!==r.top-margin||
              p.windowRect.width!==r.width+margin*2||p.windowRect.height!==r.height+margin*2)throw new Error(`Shadow helper geometry/input differs ${name} ${JSON.stringify({target:r,helper:p})}`);
          } else if(visible.length)throw new Error(`Hidden/cleared shadow still visible ${name}`);
        } else if(helpers.length)throw new Error('Native target destruction leaked helper');
        this.awaiting=true;
        const scene=JSON.stringify({native:state,main:main.getWindowProperties(),target:name==='closed'?null:target.getWindowProperties(),helpers:helpers,density:density});
        this.result=`${name}: ${scene}`;console.info(`NATIVEAPI_SHADOW_PHASE ${i} ${name} ${scene}`);
        for(let j=0;j<150&&this.awaiting;++j)await this.pause();
        if(this.awaiting)throw new Error('Scene acknowledgement timeout');
      }
      console.info(`NATIVEAPI_SHADOW_DONE ${JSON.stringify(this.state('finish'))}`);this.result='Custom shadow acceptance passed';
    }catch(error){console.error(`NATIVEAPI_SHADOW_ERROR ${error.code} ${error.message}`);}
  }
  aboutToAppear(): void {setTimeout(()=>{this.run();},400);}
  build() {Column(){Text('NativeAPI custom shadow acceptance').fontSize(22);Blank();Text(this.result).fontSize(12)}
    .width('100%').height('100%').backgroundColor('#FFFFFF')
    .onTouch((event: TouchEvent)=>{if(event.type===TouchType.Down)console.info('NATIVEAPI_SHADOW_TOUCH main');})}
}
