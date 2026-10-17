import { window } from '@kit.ArkUI';
import { BusinessError } from '@kit.BasicServicesKit';
import { WindowShadowFixture } from './WindowShadowFixture';

@Entry
@Component
struct WindowTitleProbe {
  @State result:string='Probing public window actions';
  private async pause():Promise<void> {await new Promise<void>((resolve)=>{setTimeout(resolve,400);});}
  private async run():Promise<void> {
    try {
      const stage=WindowShadowFixture.stage;
      if(!stage)throw new Error('Missing fixture Stage');
      const main=stage.getMainWindowSync();
      const child=await stage.createSubWindowWithOptions('NativeAPI Title SDK Child',{
        title:'NativeAPI Title SDK Child',decorEnabled:true,maximizeSupported:true
      });
      await child.setUIContent('pages/ControlsTarget');
      await child.resize(800,500);await child.moveWindowTo(350,500);await child.showWindow();
      const phases=['baseline','child-maximize','child-recover','child-title-buttons','child-hide-decor','child-show-decor','child-close'];
      for(let i=0;i<phases.length;++i) {
        const phase=phases[i];let code=0;
        try {
          if(phase==='child-maximize')await child.maximize(window.MaximizePresentation.EXIT_IMMERSIVE);
          else if(phase==='child-recover')await child.recover();
          else if(phase==='child-title-buttons')child.setWindowTitleButtonVisible(true,true,true);
          else if(phase==='child-hide-decor')await child.setWindowDecorVisible(false);
          else if(phase==='child-show-decor')await child.setWindowDecorVisible(true);
          else if(phase==='child-close')await child.destroyWindow();
        }catch(error){code=(error as BusinessError).code;}
        await this.pause();
        let status=-1;let statusCode=0;let properties:window.WindowProperties|undefined=undefined;
        let propertyCode=0;let freeform=false;let freeformCode=0;
        try {freeform=child.isInFreeWindowMode();}catch(error){freeformCode=(error as BusinessError).code;}
        try {status=child.getWindowStatus();}catch(error){statusCode=(error as BusinessError).code;}
        try {properties=child.getWindowProperties();}catch(error){propertyCode=(error as BusinessError).code;}
        console.info(`NATIVEAPI_TITLEPROBE_PHASE ${i} ${phase} ${JSON.stringify({code:code,status:status,statusCode:statusCode,properties:properties,propertyCode:propertyCode,freeform:freeform,freeformCode:freeformCode,main:main.getWindowProperties()})}`);
      }
      this.result='Public SDK title probe complete';
      console.info('NATIVEAPI_TITLEPROBE_DONE');
    }catch(error){console.error(`NATIVEAPI_TITLEPROBE_ERROR ${error.code} ${error.message}`);}
  }
  aboutToAppear():void {setTimeout(()=>{this.run();},450);}
  build(){Column(){Text('NativeAPI SDK title action reference');Text(this.result)}.width('100%').height('100%').backgroundColor('#FFFFFF')}
}
