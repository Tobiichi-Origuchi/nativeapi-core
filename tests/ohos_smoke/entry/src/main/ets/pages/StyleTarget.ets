@Entry
@Component
struct StyleTarget {
  build() {
    Column() { Text('NativeAPI window style target').fontSize(12) }
      .width('100%').height('100%').justifyContent(FlexAlign.Center)
  }
}
