import { window } from '@kit.ArkUI';
import { BusinessError } from '@kit.BasicServicesKit';
import nativeTests from 'libnativeapi_smoke.so';
import { WindowShadowFixture } from './WindowShadowFixture';
import { NativeTitleBar } from './NativeTitleBar';

interface TitleBounds {x:number;y:number;width:number;height:number;}
interface TitleNativeWindow {id:number;alive:boolean;bounds:TitleBounds;maximized:boolean;minimized:boolean;maximizable:boolean;events:number[];}
interface TitleNative {passed:number;failed:number;calls:number;accepted:number;lastResult:boolean;windows:TitleNativeWindow[];}
interface TitleSnapshot {native:TitleNative;windows:(window.WindowProperties|undefined)[];status:number[];showing:boolean[];density:number[];freeform:boolean[];}

@Entry
@Component
struct WindowTitle {
  @State result:string='Preparing custom title actions';
  private targets:window.Window[]=[];
  private async pause():Promise<void> {await new Promise<void>((resolve)=>{setTimeout(resolve,180);});}
  private native(command:string=''):TitleNative {return JSON.parse(nativeTests.titleState(command)) as TitleNative;}
  private snapshot():TitleSnapshot {
    const windows:Array<window.WindowProperties|undefined>=[];
    const status:number[]=[];const showing:boolean[]=[];const density:number[]=[];const freeform:boolean[]=[];
    for(const target of this.targets) {
      try {windows.push(target.getWindowProperties());}catch(_error){windows.push(undefined);}
      try {status.push(target.getWindowStatus());}catch(_error){status.push(-1);}
      try {showing.push(target.isWindowShowing());}catch(_error){showing.push(false);}
      try {density.push(target.getWindowDensityInfo().systemDensity);}catch(_error){density.push(1);}
      try {freeform.push(target.isInFreeWindowMode());}catch(_error){freeform.push(false);}
    }
    return {native:this.native(),windows:windows,status:status,showing:showing,density:density,freeform:freeform};
  }
  private async settle(index:number,wanted:number,rect?:window.Rect):Promise<void> {
    let stable=0;
    for(let i=0;i<100;++i) {
      const s=this.snapshot();const p=s.windows[index];const b=s.native.windows[index].bounds;
      const same=p!==undefined&&s.status[index]===wanted&&
        Math.abs(b.x*s.density[index]-p.windowRect.left)<0.03&&
        Math.abs(b.y*s.density[index]-p.windowRect.top)<0.03&&
        Math.abs(b.width*s.density[index]-p.windowRect.width)<0.03&&
        Math.abs(b.height*s.density[index]-p.windowRect.height)<0.03&&
        (rect===undefined||JSON.stringify(p.windowRect)===JSON.stringify(rect));
      stable=same?stable+1:0;
      if(stable>=3)return;
      await this.pause();
    }
    throw new Error(`Title action did not settle ${index} ${wanted} ${JSON.stringify(this.snapshot())}`);
  }
  private async ack(serial:number):Promise<void> {
    for(let i=0;i<200&&AppStorage.get<string>('nativeapiTitleAck')!==String(serial);++i)await this.pause();
    if(AppStorage.get<string>('nativeapiTitleAck')!==String(serial))throw new Error('Title scene acknowledgement timeout');
  }
  private async run():Promise<void> {
    try {
      const stage=WindowShadowFixture.stage;
      if(!stage)throw new Error('Missing fixture Stage');
      const main=stage.getMainWindowSync();
      if(main.getWindowStatus()!==window.WindowStatusType.FLOATING) {
        try {await main.recover();}catch(error) {
          const code=(error as BusinessError).code;
          if(code!==801)throw new Error(`Initial SDK recovery failed ${code}`);
        }
      }
      const available=main.getWindowStatus()===window.WindowStatusType.FLOATING;
      const child=await stage.createSubWindowWithOptions('NativeAPI Title Child',{
        title:'NativeAPI Title Child',decorEnabled:true,maximizeSupported:true
      });
      await child.setUIContent('pages/TitleChild');await child.resize(800,500);await child.moveWindowTo(350,500);
      const ordinary=await stage.createSubWindow('NativeAPI Ordinary Title Child');
      await ordinary.setUIContent('pages/ControlsTarget');await ordinary.resize(300,180);await ordinary.moveWindowTo(750,650);
      this.targets=[main,child,ordinary];
      let ordinaryCode=0;
      try {await ordinary.maximize(window.MaximizePresentation.EXIT_IMMERSIVE);}catch(error){ordinaryCode=(error as BusinessError).code;}
      let childButtonCode=0;
      try {child.setWindowTitleButtonVisible(true,true,true);}catch(error){childButtonCode=(error as BusinessError).code;}
      nativeTests.titleState('setup');nativeTests.titleState('attach-child',child);nativeTests.titleState('attach-ordinary',ordinary);
      this.native('hide-decor');await child.setWindowDecorVisible(false);
      await child.showWindow();await ordinary.showWindow();
      await this.settle(1,child.getWindowStatus());
      await this.settle(2,ordinary.getWindowStatus());
      const childBase=child.getWindowProperties().windowRect;
      if(childBase.width!==800||childBase.height!==500||childBase.left!==350||childBase.top!==500)
        throw new Error(`Child was not initially measured ${JSON.stringify(childBase)}`);
      await child.minimize();await ordinary.minimize();
      await this.settle(0,available?4:main.getWindowStatus());
      const mainBase=main.getWindowProperties().windowRect;
      console.info(`NATIVEAPI_TITLE_REFERENCE ${JSON.stringify({available:available,ordinaryCode:ordinaryCode,childButtonCode:childButtonCode,mainBase:mainBase,childBase:childBase,snapshot:this.snapshot()})}`);
      const phases=['baseline','main-disabled-cpp-ui','main-disabled-c-worker','main-cpp-ui-max','main-c-ui-recover',
        'main-cpp-worker-max','main-c-worker-recover','main-overlap-cpp-ui','main-overlap-c-ui-recover',
        'main-maximized-disabled','main-enable-recover','main-fullscreen','main-minimized','main-floating-max','main-final-recover',
        'child-ready','child-cpp-ui-max','child-c-ui-recover','child-cpp-worker-max','child-c-worker-recover',
        'child-overlap-cpp-ui','child-final-recover','ordinary-ui-refusal','ordinary-worker-refusal','closed-child-ui','closed-child-worker',
        'invalid','restore-decor','split-ready','main-split-c-worker-recover','finish'];
      for(let i=0;i<phases.length;++i) {
        const name=phases[i];const serial=i+1;let target=0;let useC=false;let worker=false;
        let gesture=false;let overlap=false;let expected=false;let wanted=available?4:main.getWindowStatus();
        if(name==='main-disabled-cpp-ui'||name==='main-maximized-disabled')this.native('disable');
        if(name==='main-cpp-ui-max'||name==='main-enable-recover')this.native('enable');
        if(name==='main-maximized-disabled'&&available){await main.maximize(window.MaximizePresentation.EXIT_IMMERSIVE);await this.settle(0,2);}
        if(name==='main-fullscreen'&&available){await main.maximize(window.MaximizePresentation.ENTER_IMMERSIVE);await this.settle(0,1);}
        if(name==='main-minimized'&&available){await main.recover();await this.settle(0,4,mainBase);await main.minimize();await this.settle(0,3,mainBase);}
        if(name==='child-ready') {await child.showWindow();await this.pause();}
        if(name==='ordinary-ui-refusal') {await ordinary.showWindow();await this.settle(2,ordinary.getWindowStatus());}
        if(name==='closed-child-ui') {await child.destroyWindow();await ordinary.destroyWindow();await this.pause();}
        if(name==='invalid')this.native('invalid');
        if(name==='restore-decor') {
          this.native('show-decor');
          if(available) {
            for(let j=0;j<100&&main.getWindowProperties().drawableRect.top===0;++j)await this.pause();
            if(main.getWindowProperties().drawableRect.top===0)throw new Error('Native decoration did not return');
          }
        }
        if(name==='main-split-c-worker-recover'&&available) {
          if(main.getWindowStatus()!==5)throw new Error('Real native split menu did not enter split-screen mode');
          await this.settle(0,5);
        }
        if(name==='finish')this.native('finish');
        const mainGesture=name.startsWith('main-')&&name!=='main-minimized';
        const childGesture=name.startsWith('child-')&&name!=='child-ready';
        gesture=mainGesture||childGesture;
        target=childGesture||name.startsWith('closed-child')?1:name.startsWith('ordinary-')?2:0;
        useC=name.includes('-c-')||name==='main-floating-max'||name==='child-final-recover'||name==='closed-child-worker'||name==='ordinary-worker-refusal';
        worker=name.includes('worker')||name==='main-floating-max';
        overlap=name.includes('overlap');
        expected=available&&(mainGesture||childGesture)&&!name.includes('disabled')&&name!=='main-fullscreen';
        if(available&&gesture)wanted=expected?(name.includes('recover')?4:2):(name==='main-fullscreen'?1:name==='main-maximized-disabled'?2:4);
        else if(available&&name==='main-minimized')wanted=3;
        else if(target===1)wanted=this.snapshot().status[1];
        const before=this.snapshot();
        AppStorage.set('nativeapiTitleTarget',target);AppStorage.set('nativeapiTitleUseC',useC);
        AppStorage.set('nativeapiTitleWorker',worker);AppStorage.set('nativeapiTitleExpected',expected);
        AppStorage.set('nativeapiTitleOverlap',overlap);AppStorage.set('nativeapiTitleSerial',serial);
        AppStorage.set('nativeapiTitleArmed',gesture);
        console.info(`NATIVEAPI_TITLE_WAIT ${i} ${name} ${JSON.stringify({gesture:gesture,target:target,expected:expected,worker:worker,useC:useC,overlap:overlap,before:before})}`);
        if(gesture) {
          for(let j=0;j<150&&AppStorage.get<number>('nativeapiTitleCompleted')!==serial;++j)await this.pause();
          if(AppStorage.get<number>('nativeapiTitleCompleted')!==serial)throw new Error('Missing real custom title gesture');
        } else if(name==='main-minimized') {
          await nativeTests.titleAction(0,false,false,false,false);
          await nativeTests.titleAction(0,true,true,false,false);
        } else if(name.startsWith('ordinary-')||name.startsWith('closed-child')) {
          await nativeTests.titleAction(target,useC,worker,false,false);
        }
        if(name!=='finish') {
          if(gesture)await this.settle(target,wanted,available&&expected&&wanted===4?(target===0?mainBase:childBase):undefined);
          else if(name==='main-minimized')await this.settle(0,wanted,mainBase);
          this.native('verify');
        }
        const after=this.snapshot();
        if(after.native.failed)throw new Error('Native title assertions failed');
        this.result=name;
        console.info(`NATIVEAPI_TITLE_PHASE ${i} ${name} ${JSON.stringify({snapshot:after,target:target,gesture:gesture,expected:expected,wanted:wanted})}`);
        await this.ack(serial);
        if(name==='main-minimized'&&available){await main.restore();await this.settle(0,4,mainBase);}
      }
      AppStorage.set('nativeapiTitleArmed',false);
      this.result='Custom title acceptance passed';
      console.info(`NATIVEAPI_TITLE_DONE ${JSON.stringify(this.native())}`);
    }catch(error){console.error(`NATIVEAPI_TITLE_ERROR ${error.code} ${error.message}`);}
  }
  aboutToAppear():void {setTimeout(()=>{this.run();},450);}
  build(){Column(){NativeTitleBar({target:0});Text('NativeAPI custom title action acceptance').fontSize(22);Text(this.result).fontSize(18);Blank()}.width('100%').height('100%').backgroundColor('#FFFFFF')}
}
