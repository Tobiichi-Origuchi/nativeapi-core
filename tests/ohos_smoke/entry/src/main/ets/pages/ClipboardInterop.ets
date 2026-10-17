@Entry
@Component
struct ClipboardInterop {
  aboutToAppear(): void { setTimeout(() => { console.info('NATIVEAPI_CLIPBOARD_READY'); }, 500); }
  build() { Column({ space: 20 }) { Text('NativeAPI Clipboard Interoperability').fontSize(22)
    Text('Independent app transfers and sandbox validation').fontSize(14)
  }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF') }
}
