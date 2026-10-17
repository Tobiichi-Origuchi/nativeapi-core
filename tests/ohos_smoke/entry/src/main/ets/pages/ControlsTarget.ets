@Entry
@Component
struct ControlsTarget {
  build() {
    Column() { Text('NativeAPI controls child').fontSize(16) }
      .width('100%').height('100%').backgroundColor('#FF00FF').justifyContent(FlexAlign.Center)
  }
}
