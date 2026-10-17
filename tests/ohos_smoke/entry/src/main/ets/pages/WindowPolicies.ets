import { window } from '@kit.ArkUI';
import { common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import nativeTests from 'libnativeapi_smoke.so';
interface PolicyState {ready:boolean;alive:boolean;visible:boolean;policy:boolean[];childPolicy:boolean[];policyObjects:number[];policyManagers:number[];policyC:number[];passed:number;failed:number;}
@Entry
@Component
struct WindowPolicies {
  @StorageLink('nativeapiPolicyAction') @Watch('command') action:string='';
  @State result:string='Preparing caption policies';
  private awaiting:boolean=false;
  private command():void {if(this.action)this.awaiting=false;}
  private async pause():Promise<void> {await new Promise<void>((resolve)=>{setTimeout(resolve,180);});}
  private state(command:string=''):PolicyState {return JSON.parse(nativeTests.controlsState(command)) as PolicyState;}
  private async run():Promise<void> {
    try {
      const main=await window.getLastWindow(this.getUIContext().getHostContext() as common.UIAbilityContext);
      let mainCode=0;
      try {main.setWindowTitleButtonVisible(true,true,true);}catch(error){mainCode=(error as BusinessError).code;}
      const available=mainCode===0;
      if(!available&&mainCode!==801)throw new Error(`Unexpected caption SDK result ${mainCode}`);
      nativeTests.controlsState('setup');
      for(let j=0;j<100&&!this.state().ready;++j)await this.pause();
      if(!this.state().ready)throw new Error('Missing policy child');
      const child=window.findWindow('NativeAPI Controls Child');let childCode=0;
      try {child.setWindowTitleButtonVisible(true,true,true);}catch(error){childCode=(error as BusinessError).code;}
      if(childCode!==1300004&&childCode!==801)throw new Error(`Unexpected child policy SDK result ${childCode}`);
      await this.pause();
      console.info(`NATIVEAPI_POLICY_REFERENCE ${JSON.stringify({mainCode:mainCode,childCode:childCode,main:main.getWindowProperties(),child:child.getWindowProperties()})}`);
      const phases=['baseline','min-off','min-off-again','max-off','close-off','max-hidden-double-click','recover-after-hidden-double','program-max','program-recover','close-on','collective-hide','min-on-hidden','collective-show','hide-decor','max-on-hidden','show-decor','sdk-close-only','max-off-after-sdk','min-off-second','max-on','user-maximize','recover-after-user','enabled-double-click','recover-after-double','min-off-repeat','program-minimize','program-restore','policy-child','close-off-final','close-child','main-close-refused','finish'];
      const policy=available?[true,true,true]:[false,false,false];let shown=true;
      const baseline=this.state().policyObjects;const events=[...baseline];
      for(let i=0;i<phases.length;++i) {
        const name=phases[i];let command='';let index=-1;let value=false;
        if(['min-off','min-off-again','min-off-second','min-off-repeat'].includes(name)){command='min-off';index=0;}
        else if(name==='min-on-hidden'){command='min-on';index=0;value=true;}
        else if(name==='max-off'||name==='max-off-after-sdk'){command='max-off';index=1;}
        else if(name==='max-on'||name==='max-on-hidden'){command='max-on';index=1;value=true;}
        else if(name==='close-off'||name==='close-off-final'){command='close-off';index=2;}
        else if(name==='close-on'){command='close-on';index=2;value=true;}
        else if(name==='collective-hide'){command='hide';shown=false;}
        else if(name==='collective-show'){command='show';shown=true;}
        else if(name==='hide-decor'){command=name;shown=false;}
        else if(name==='show-decor'){command=name;shown=true;}
        else if(name==='policy-child')command=name;
        else if(name==='close-child')command='close';
        else if(name==='main-close-refused')command=name;
        else if(available&&(name==='program-max'))command='maximize';
        else if(available&&(name==='program-recover'||name==='recover-after-user'||name==='recover-after-double'||name==='recover-after-hidden-double'))command='recover';
        else if(available&&(name==='program-minimize'||name==='program-restore'))command=name;
        if(index>=0&&available&&policy[index]!==value){policy[index]=value;++events[index];}
        if(command)await nativeTests.controlsAction(command,i%2===0,i%3!==0);
        if(name==='sdk-close-only'&&available)main.setWindowTitleButtonVisible(false,false,true);
        if(name==='finish')nativeTests.controlsState('policy-finish');
        const wantedStatus=available&&(name==='program-max')?2:available&&name==='program-minimize'?3:available?4:main.getWindowStatus();
        const visible=available&&shown&&(name==='sdk-close-only'||policy.some((v:boolean)=>v));
        for(let j=0;j<100;++j) {
          const s=this.state();
          const ok=s.policy.every((v:boolean,k:number)=>v===policy[k])&&s.policyObjects.every((v:number,k:number)=>v===events[k])&&(name==='program-minimize'&&available||s.visible===visible)&&
            s.policyManagers.every((v:number,k:number)=>v===events[k])&&s.policyC.every((v:number,k:number)=>v===events[k])&&main.getWindowStatus()===wantedStatus&&
            (name!=='close-child'&&name!=='finish'||!s.alive);
          if(ok)break;
          if(j===99)throw new Error(`Caption policy did not settle ${name} ${JSON.stringify({native:s,policy:policy,events:events,status:main.getWindowStatus(),expectedStatus:wantedStatus,visible:visible})}`);
          await this.pause();
        }
        await this.pause();
        const s=this.state(name==='finish'?'':'policy-verify');if(s.failed)throw new Error('Native policy assertions failed');
        if(s.childPolicy.some((v:boolean)=>v))throw new Error('Ordinary/closed child exposes caption policy');
        this.awaiting=true;this.result=name;
        console.info(`NATIVEAPI_POLICY_PHASE ${i} ${name} ${JSON.stringify({native:s,main:main.getWindowProperties(),status:main.getWindowStatus(),available:available,policy:policy,shown:shown})}`);
        for(let j=0;j<200&&this.awaiting;++j)await this.pause();
        if(this.awaiting)throw new Error('Policy scene acknowledgement timeout');
        if(['user-maximize','enabled-double-click','max-hidden-double-click'].includes(name)&&available) {
          for(let j=0;j<100&&main.getWindowStatus()!==2;++j)await this.pause();
          if(main.getWindowStatus()!==2)throw new Error('Real native maximize button did not maximize');
          console.info(`NATIVEAPI_POLICY_GESTURE ${name} ${JSON.stringify({main:main.getWindowProperties(),status:main.getWindowStatus()})}`);
        }
      }
      console.info(`NATIVEAPI_POLICY_DONE ${JSON.stringify(this.state())}`);this.result='Caption policy acceptance passed';
    }catch(error){console.error(`NATIVEAPI_POLICY_ERROR ${error.code} ${error.message}`);}
  }
  aboutToAppear():void {setTimeout(()=>{this.run();},450);}
  build() {Column(){Text('NativeAPI caption policy acceptance').fontSize(22);Text(this.result).fontSize(18);Blank()}
    .width('100%').height('100%').backgroundColor('#FFFFFF')}
}
