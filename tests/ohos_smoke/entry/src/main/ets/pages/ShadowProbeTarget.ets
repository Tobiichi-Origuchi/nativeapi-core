@Entry
@Component
struct ShadowProbeTarget {
  build() { Stack() { Text('Shadow target').fontSize(18) }.width('100%').height('100%').backgroundColor('#FF00FF')
    .onTouch((event: TouchEvent)=>{if(event.type===TouchType.Down)console.info('NATIVEAPI_SHADOW_TOUCH target');}) }
}
