import { window } from '@kit.ArkUI';
import { BusinessError } from '@kit.BasicServicesKit';
import nativeTests from 'libnativeapi_smoke.so';
@Entry
@Component
struct DragTarget {
  build() {
    Column() {
      Text('NativeAPI Child Drag Pad').id('nativeapi-child-drag-pad').fontSize(18)
        .width('100%').height('100%').textAlign(TextAlign.Center)
        .onTouch((event: TouchEvent) => {
          if(event.type !== TouchType.Down) { return; }
          const mode = AppStorage.get<string>('nativeapiDragMode') ?? 'off';
          console.info(`NATIVEAPI_DRAG_DOWN child ${mode}`);
          if(mode==='cpp' || mode==='c') { nativeTests.dragStart(false,mode==='c'); }
          if(mode==='sdk') {
            window.findWindow('NativeAPI Drag Target').startMoving().then(() => {
              console.info('NATIVEAPI_DRAG_REFERENCE child code=0');
            }).catch((error: BusinessError) => { console.info(`NATIVEAPI_DRAG_REFERENCE child code=${error.code}`); });
          }
        })
    }.width('100%').height('100%').backgroundColor('#FF00FF')
  }
}
