import { window } from '@kit.ArkUI';
import { BusinessError } from '@kit.BasicServicesKit';
import nativeTests from 'libnativeapi_smoke.so';
import { WindowShadowFixture } from './WindowShadowFixture';

interface MainShadowState {passed: number;failed: number;configured: boolean;enabled: boolean;alive: boolean;color: number;blur: number;offset: number[];}
interface MainShadowHelper {properties: window.WindowProperties;showing: boolean;}
@Entry
@Component
struct WindowShadowMain {
  @StorageLink('nativeapiShadowAction') @Watch('command') action: string = '';
  @State result: string = 'Preparing primary-window custom shadow';
  private awaiting: boolean = false;
  private command(): void {if(this.action)this.awaiting=false;}
  private async pause(): Promise<void> {await new Promise<void>((resolve)=>{setTimeout(resolve,180);});}
  private state(verify: boolean = false): MainShadowState {return JSON.parse(nativeTests.shadowState(verify?'verify':'')) as MainShadowState;}
  private helpers(id: number): MainShadowHelper[] {
    const values: MainShadowHelper[]=[];
    for(let i=1;i<=16;++i)try {const helper=window.findWindow(`NativeAPI shadow ${id} ${i}`);values.push({properties:helper.getWindowProperties(),showing:helper.isWindowShowing()});}catch(_){}
    return values;
  }
  private async run(): Promise<void> {
    try {
      const stage=WindowShadowFixture.stage;if(!stage)throw new Error('Missing Stage');
      const main=stage.getMainWindowSync();
      let shadowCode=0,decorCode=0;
      try {await main.setWindowShadowEnabled(false);}catch(error){shadowCode=(error as BusinessError).code;}
      try {await main.setWindowDecorVisible(false);}catch(error){decorCode=(error as BusinessError).code;}
      if(shadowCode!==0&&shadowCode!==801)throw new Error(`Unexpected primary shadow reference ${shadowCode}`);
      console.info(`NATIVEAPI_MAINSHADOW_REFERENCE ${JSON.stringify({shadowCode:shadowCode,decorCode:decorCode,main:main.getWindowProperties()})}`);
      nativeTests.shadowTargetAttach(main,true);
      await this.pause();
      const id=main.getWindowProperties().id,density=main.getWindowDensityInfo().systemDensity;
      const phases=shadowCode===0?['baseline','disabled-config','soft','hard-red','hard-green','helper-recreated','disabled','reenabled','moved','resized','decorated','decorated-refused','undecorated','cleared','default-off','default-on','default-reference','restored']:
        ['baseline','refused-cpp','refused-c','refused-ui','refused-clear','restored'];
      let color=0,blur=0,enabled=false,configured=false;
      const original=main.getWindowProperties().windowRect;
      let expectedLeft=original.left,expectedTop=original.top,expectedWidth=original.width,expectedHeight=original.height;
      for(let i=0;i<phases.length;++i) {
        const name=phases[i],focused=main.isFocused();let ok=true;
        if(shadowCode===0) {
          if(name==='disabled-config'){ok=await nativeTests.shadowAction('soft',false,true);configured=true;color=0xff0000c0;blur=18;}
          else if(name==='soft'||name==='reenabled'||name==='default-on'){ok=await nativeTests.shadowAction('enable',name==='reenabled',true);enabled=true;}
          else if(name==='hard-red'){ok=await nativeTests.shadowAction('hard',true,true);color=0xff0000c0;blur=0;}
          else if(name==='hard-green'){ok=await nativeTests.shadowAction('green',false,false);color=0x00ff00c0;blur=0;}
          else if(name==='helper-recreated') {
            const active=this.helpers(id).filter((h: MainShadowHelper)=>h.showing);
            if(active.length!==1)throw new Error('Missing helper before native destruction');
            await window.findWindow(active[0].properties.name).destroyWindow();
            ok=await nativeTests.shadowAction('green',true,true);
          }
          else if(name==='disabled'||name==='default-off'){ok=await nativeTests.shadowAction('disable',true,true);enabled=false;}
          else if(name==='moved'){expectedLeft=original.left+60;expectedTop=original.top+40;await main.moveWindowTo(expectedLeft,expectedTop);}
          else if(name==='resized'){expectedWidth=original.width+80;expectedHeight=original.height+60;await main.resize(expectedWidth,expectedHeight);}
          else if(name==='decorated'||name==='undecorated')ok=await nativeTests.shadowAction(name==='decorated'?'title-normal':'title-hidden',false,true);
          else if(name==='decorated-refused')ok=!(await nativeTests.shadowAction('soft',true,true));
          else if(name==='cleared'){ok=await nativeTests.shadowAction('clear',true,true);configured=false;color=0;blur=0;}
          else if(name==='default-reference')await main.setWindowShadowEnabled(true);
          else if(name==='restored') {
            expectedLeft=original.left;expectedTop=original.top;expectedWidth=original.width;expectedHeight=original.height;
            await main.resize(original.width,original.height);await main.moveWindowTo(original.left,original.top);
            await nativeTests.shadowAction('title-normal',false,true);
          }
          if(!ok)throw new Error(`Primary native request refused ${name}`);
        } else if(name!=='baseline'&&name!=='restored') {
          ok=await nativeTests.shadowAction(name==='refused-clear'?'clear':'soft',name==='refused-c',name!=='refused-ui');
          if(ok)throw new Error(`Primary unsupported request unexpectedly succeeded ${name}`);
        }
        for(let j=0;j<100;++j) {
          const s=this.state(),helpers=this.helpers(id),visible=helpers.filter((h: MainShadowHelper)=>h.showing);
          const r=main.getWindowProperties().windowRect;
          // The old main frame and old helper can briefly agree after resize.
          // Require the explicitly requested frame before accepting that match.
          let geometry=r.left===expectedLeft&&r.top===expectedTop&&r.width===expectedWidth&&r.height===expectedHeight;
          if(configured&&enabled&&!['decorated','decorated-refused'].includes(name)) {
            const margin=Math.ceil(blur*density/3)*3+Math.ceil(12*density)+2;
            geometry=geometry&&visible.length===1&&visible[0].properties.windowRect.left===r.left-margin&&visible[0].properties.windowRect.top===r.top-margin&&
              visible[0].properties.windowRect.width===r.width+margin*2&&visible[0].properties.windowRect.height===r.height+margin*2;
          } else geometry=geometry&&visible.length===0;
          if(s.configured===configured&&s.enabled===enabled&&s.color===color&&s.blur===blur&&geometry)break;
          if(j===99)throw new Error(`Primary state/geometry failed ${name} ${JSON.stringify({native:s,helpers:helpers,main:main.getWindowProperties()})}`);
          await this.pause();
        }
        await this.pause();
        const s=this.state(true);if(s.failed)throw new Error('Primary C++/C checks failed');
        const helpers=this.helpers(id);
        if(helpers.some((h: MainShadowHelper)=>h.properties.focusable||h.properties.touchable))throw new Error('Interactive shadow helper');
        if(main.isFocused()!==focused)throw new Error(`Primary shadow stole focus ${name}`);
        if((!configured||shadowCode!==0)&&helpers.length)throw new Error(`Primary refusal/clear leaked helper ${name}`);
        const scene=JSON.stringify({native:s,main:main.getWindowProperties(),helpers:helpers,density:density,shadowCode:shadowCode,decorCode:decorCode});
        this.awaiting=true;this.result=name;console.info(`NATIVEAPI_MAINSHADOW_PHASE ${i} ${name} ${scene}`);
        for(let j=0;j<200&&this.awaiting;++j)await this.pause();
        if(this.awaiting)throw new Error('Primary scene acknowledgement timeout');
      }
      console.info(`NATIVEAPI_MAINSHADOW_DONE ${JSON.stringify(this.state())}`);nativeTests.shadowState('finish');
      this.result='Primary-window custom shadow acceptance passed';
    }catch(error){console.error(`NATIVEAPI_MAINSHADOW_ERROR ${error.code} ${error.message}`);}
  }
  aboutToAppear(): void {setTimeout(()=>{this.run();},450);}
  build() {Column(){Text('NativeAPI primary-window custom shadow acceptance').fontSize(22);Text(this.result).fontSize(16);
    Button('NativeAPI primary input probe').id('nativeapi-main-shadow-content').onClick(()=>{console.info('NATIVEAPI_MAINSHADOW_CLICK main');});Blank()}
    .width('100%').height('100%').backgroundColor('#FF00FF')
    .onTouch((event: TouchEvent)=>{if(event.type===TouchType.Down)console.info('NATIVEAPI_MAINSHADOW_TOUCH main');})}
}
