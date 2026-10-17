import { image } from '@kit.ImageKit';
@Entry({ useSharedStorage: true })
@Component
struct ShadowProbeSurface {
  @LocalStorageLink('pixels') pixels?: image.PixelMap = undefined;
  build() { Stack() { Image(this.pixels).width('100%').height('100%').objectFit(ImageFit.Fill).interpolation(ImageInterpolation.None) }
    .width('100%').height('100%').backgroundColor('#00000000').hitTestBehavior(HitTestMode.None) }
}
