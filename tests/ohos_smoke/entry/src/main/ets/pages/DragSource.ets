import { unifiedDataChannel, uniformDataStruct, uniformTypeDescriptor } from '@kit.ArkData';
import { fileIo } from '@kit.CoreFileKit';
import { util } from '@kit.ArkTS';
import nativeTests from 'libnativeapi_smoke.so';
@Entry
@Component
struct DragSource {
  @StorageLink('nativeapiSourceScene') scene: string = '';
  aboutToAppear(): void { setTimeout(() => { console.info('NATIVEAPI_SOURCE_READY'); }, 500); }
  build() {
    Column({ space: 16 }) {
      Text('NativeAPI source · SDK destination').fontSize(20)
      Text('Native source touch pad').id('nativeapi-source-pad').width('100%').height(90).backgroundColor('#E8E8E8')
        .onTouch((event: TouchEvent) => { if (event.type === TouchType.Down) nativeTests.sourceBegin(); })
        .onMouse((event: MouseEvent) => { if (event.action === MouseAction.Press && event.button === MouseButton.Left) nativeTests.sourceBegin(); })
      Text('Independent SDK destination').id('nativeapi-source-target').width('100%').layoutWeight(1)
        .backgroundColor('#B8E8FF').allowDrop(['general.plain-text', 'general.file-uri', 'general.file'])
        .onDrop((event: DragEvent) => {
          try {
            const records = event.getData().getRecords();
            let text = '';
            let files: string[] = [];
            for (const record of records) {
              if (record.getTypes().includes(uniformTypeDescriptor.UniformDataType.PLAIN_TEXT)) {
                text = (record as unifiedDataChannel.PlainText).textContent;
              }
              if (record.getTypes().includes(uniformTypeDescriptor.UniformDataType.FILE_URI)) {
                const entry = record.getEntry(uniformTypeDescriptor.UniformDataType.FILE_URI) as uniformDataStruct.FileUri;
                const file = fileIo.openSync(entry.oriUri, fileIo.OpenMode.READ_ONLY);
                const size = fileIo.statSync(file.fd).size;
                const buffer = new ArrayBuffer(size);
                fileIo.readSync(file.fd, buffer);
                fileIo.closeSync(file);
                files.push(new util.TextDecoder('utf-8').decodeToString(new Uint8Array(buffer)));
              }
            }
            const wantedText = this.scene.includes('files') ? '' : 'NativeAPI source 中文 🙂';
            const wantedFiles = this.scene.includes('files') || this.scene.includes('mixed') ? 2 : 0;
            if (text !== wantedText || files.length !== wantedFiles ||
                files.some((value: string, i: number) => value !== `NativeAPI native file ${i} 中文 +#%`)) {
              console.error(`NATIVEAPI_SOURCE_ERROR SDK payload ${this.scene} ${JSON.stringify({text,files})}`);
              event.setResult(DragResult.DRAG_FAILED); return;
            }
            if (this.scene.includes('target-move')) { event.dragBehavior = DragBehavior.MOVE; }
            event.setResult(this.scene.includes('refuse') ? DragResult.DRAG_FAILED : DragResult.DRAG_SUCCESSFUL);
            console.info(`NATIVEAPI_SOURCE_SDK_DROP ${this.scene} ${JSON.stringify({text,files})}`);
          } catch (error) { console.error(`NATIVEAPI_SOURCE_ERROR SDK read ${JSON.stringify(error)}`); }
        })
    }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF')
  }
}
