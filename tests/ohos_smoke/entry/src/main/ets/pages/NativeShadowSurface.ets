import { NativeWindowShadowSurface } from '../nativeapi/NativeWindowShadowHost';
@Entry({ useSharedStorage: true })
@Component
struct ShadowSurface { build() {Stack(){NativeWindowShadowSurface()}.width('100%').height('100%').backgroundColor('#00000000')} }
