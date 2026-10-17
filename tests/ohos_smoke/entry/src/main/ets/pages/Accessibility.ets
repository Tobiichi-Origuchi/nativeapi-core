import { accessibility } from '@kit.AccessibilityKit';
import nativeTests from 'libnativeapi_smoke.so';
@Entry
@Component
struct Accessibility {
  @State result: string = 'Running accessibility state checks';
  @StorageLink('nativeapiAccessibilityRun') @Watch('runTests') runId: string = 'boot';
  aboutToAppear(): void { this.runTests(); }
  private runTests(): void {
    try {
      const expected = accessibility.isOpenAccessibilitySync();
      const enabled = accessibility.getAccessibilityExtensionListSync('all', 'enable');
      console.info(`NATIVEAPI_ACCESSIBILITY_SDK ${expected} ${JSON.stringify(enabled.map((entry: accessibility.AccessibilityAbilityInfo) => entry.id))}`);
      nativeTests.runAccessibility(expected, enabled.length, this.runId).then((report: string) => { this.result = report; });
    } catch {
      this.result = 'NATIVEAPI_ACCESSIBILITY_REFERENCE_FAILED';
      console.error(this.result);
    }
  }
  build() {
    Scroll() {
      Column({ space: 16 }) {
        Text('NativeAPI Accessibility Acceptance').fontSize(24)
        Text(this.result).fontSize(16)
      }.padding(24).width('100%')
    }.width('100%').height('100%')
  }
}
