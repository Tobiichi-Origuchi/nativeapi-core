import { NodeContent, window } from '@kit.ArkUI';
import { common } from '@kit.AbilityKit';
import { unifiedDataChannel, uniformDataStruct, uniformTypeDescriptor } from '@kit.ArkData';
import { fileIo, fileUri } from '@kit.CoreFileKit';
import nativeTests from 'libnativeapi_smoke.so';
@Entry
@Component
struct DropTarget {
  @StorageLink('nativeapiDropFiles') files: boolean = false;
  @StorageLink('nativeapiDropFilesOnly') filesOnly: boolean = false;
  private paths: string[] = [];
  private content: NodeContent = new NodeContent();
  aboutToAppear(): void {
    const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
    for (let i = 0; i < 2; ++i) {
      const path = `${context.filesDir}/drag 中文 +#% ${i}.txt`;
      const file = fileIo.openSync(path, fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.TRUNC);
      fileIo.writeSync(file.fd, `NativeAPI SDK file ${i} 中文 +#%`);
      fileIo.closeSync(file);
      this.paths.push(path);
    }
    window.getLastWindow(context).then((owner: window.Window) => {
      if (!nativeTests.attachViewContent(owner, this.content)) {
        console.error('NATIVEAPI_DROP_ERROR attach'); return;
      }
      setTimeout(() => { console.info('NATIVEAPI_DROP_READY'); }, 500);
    }).catch(() => { console.error('NATIVEAPI_DROP_ERROR window'); });
  }
  aboutToDisappear(): void { nativeTests.detachViewContent(); }
  build() {
    Column({ space: 16 }) {
      Text('NativeAPI destination · SDK source').fontSize(20)
      Text('NativeAPI drag 中文 🙂').id('nativeapi-drop-source')
        .width('100%').height(80).backgroundColor('#E8E8E8').draggable(true)
        .onDragStart((event: DragEvent) => {
          const plain = new unifiedDataChannel.PlainText();
          plain.textContent = 'NativeAPI drag 中文 🙂';
          const data = new unifiedDataChannel.UnifiedData();
          if (!this.filesOnly) { data.addRecord(plain); }
          if (this.files) {
            for (const path of this.paths) {
              const record = new unifiedDataChannel.UnifiedRecord();
              const file: uniformDataStruct.FileUri = { uniformDataType: 'general.file-uri',
                oriUri: fileUri.getUriFromPath(path), fileType: 'general.plain-text' };
              record.addEntry(uniformTypeDescriptor.UniformDataType.FILE_URI, file);
              data.addRecord(record);
            }
          }
          event.setData(data);
          console.info(`NATIVEAPI_DROP_SDK_START files=${this.files} types=${JSON.stringify(data.getTypes())}`);
          return { extraInfo: 'NativeAPI SDK source' };
        })
        .onDragEnd((event: DragEvent) => { console.info(`NATIVEAPI_DROP_SDK_END ${event.getResult()}`); })
      Column() { ContentSlot(this.content) }.id('nativeapi-drop-target').width('100%').layoutWeight(1)
    }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF')
  }
}
