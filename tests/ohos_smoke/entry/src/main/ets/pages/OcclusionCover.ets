@Entry
@Component
struct OcclusionCover {
  build() {
    Column() { Text('NativeAPI occlusion cover').id('nativeapi-occlusion-cover-label').fontSize(12) }
      .width('100%').height('100%').justifyContent(FlexAlign.Center)
  }
}
