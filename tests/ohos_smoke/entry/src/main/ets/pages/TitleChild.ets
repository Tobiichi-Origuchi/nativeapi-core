import { NativeTitleBar } from './NativeTitleBar';
@Entry
@Component
struct TitleChild {
  build(){Column(){NativeTitleBar({target:1});Text('NativeAPI custom child title acceptance');Blank()}.width('100%').height('100%').backgroundColor('#FFFFFF')}
}
