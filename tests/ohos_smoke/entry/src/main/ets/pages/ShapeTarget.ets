@Entry
@Component
struct ShapeTarget {
  build() {
    Column() {}.width('100%').height('100%').backgroundColor('#FF00FF')
      .onTouch((event: TouchEvent) => {
        if (event.type === TouchType.Down) { console.info('NATIVEAPI_SHAPE_TOUCH target'); }
      })
  }
}
