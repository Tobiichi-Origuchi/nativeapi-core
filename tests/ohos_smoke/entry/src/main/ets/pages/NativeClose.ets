import nativeTests from 'libnativeapi_smoke.so';
@Entry
@Component
struct NativeClose {
  aboutToAppear(): void {
    nativeTests.nativeCloseState('setup', 'initial');
    console.info('NATIVEAPI_NATIVE_CLOSE_READY');
  }
  build() {
    Column({ space: 16 }) {
      Text('NativeAPI System Close Acceptance').fontSize(20)
      Text('Controlled close requests in this disposable emulator').fontSize(14)
    }.width('100%').height('100%').padding(30).backgroundColor('#FFFFFF')
  }
}
