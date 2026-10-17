@Entry
@Component
struct Child {
  build() {
    Column() {
      Text('NativeAPI child window').fontSize(20)
      Text('HarmonyOS window management').fontSize(14)
    }.width('100%').height('100%').justifyContent(FlexAlign.Center).backgroundColor('#DBEAFE')
  }
}
