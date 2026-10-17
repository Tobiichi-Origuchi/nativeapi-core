@Entry
@Component
struct Index {
  @StorageLink('peerStatus') status: string = 'Preparing';
  build() { Column({ space: 20 }) {
    Text('Independent SDK Clipboard Peer').fontSize(24)
    Text(this.status).fontSize(14)
    Button('Independent input focus').defaultFocus(true)
  }.width('100%').height('100%').padding(24) .backgroundColor('#FFFFFF')
    .onKeyPreIme((event: KeyEvent): boolean => {
      if (event.keyCode === 2098 && event.type === KeyType.Down) {
        console.info(`NATIVEAPI_PEER_LOCAL_F9 ctrl=${event.getModifierKeyState?.(['Ctrl'])} alt=${event.getModifierKeyState?.(['Alt'])}`);
      }
      return false;
    }) }
}
