import nativeTests from 'libnativeapi_smoke.so';

@Component
export struct NativeTitleBar {
  @Prop target:number=0;
  @StorageLink('nativeapiTitleArmed') armed:boolean=false;
  @StorageLink('nativeapiTitleTarget') selected:number=0;
  @StorageLink('nativeapiTitleSerial') serial:number=0;
  @StorageLink('nativeapiTitleUseC') useC:boolean=false;
  @StorageLink('nativeapiTitleWorker') worker:boolean=false;
  @StorageLink('nativeapiTitleExpected') expected:boolean=false;
  @StorageLink('nativeapiTitleOverlap') overlap:boolean=false;
  private async perform():Promise<void> {
    if(!this.armed)return;
    if(this.selected!==this.target) {
      console.error('NATIVEAPI_TITLE_ERROR wrong custom title target');return;
    }
    this.armed=false;
    const serial=this.serial;
    try {
      const state=await nativeTests.titleAction(this.target,this.useC,this.worker,this.expected,this.overlap);
      console.info(`NATIVEAPI_TITLE_GESTURE ${serial} ${state}`);
      AppStorage.set('nativeapiTitleCompleted',serial);
    }catch(error){console.error(`NATIVEAPI_TITLE_ERROR gesture ${error.code} ${error.message}`);}
  }
  build(){
    Row(){Text(this.target===0?'NativeAPI custom main title':'NativeAPI custom child title').fontSize(17).fontColor('#FFFFFF')}
      .id(this.target===0?'nativeapi-title-main-bar':'nativeapi-title-child-bar')
      .width('100%').height(52).padding({left:16}).backgroundColor('#2858B8')
      .gesture(TapGesture({count:2}).onAction(()=>{this.perform();}))
  }
}
