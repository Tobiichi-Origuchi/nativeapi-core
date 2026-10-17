@Entry
@Component
struct OcclusionTarget {
  build() {
    Column() { Text('NativeAPI occlusion target').id('nativeapi-occlusion-target-label').fontSize(12) }
      .width('100%').height('100%').justifyContent(FlexAlign.Center)
  }
}
