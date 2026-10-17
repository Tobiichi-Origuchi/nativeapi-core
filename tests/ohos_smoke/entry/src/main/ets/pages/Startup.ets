import { autoStartupManager } from '@kit.AbilityKit';
import nativeTests from 'libnativeapi_smoke.so';
@Entry
@Component
struct Startup {
  @StorageLink('nativeapiStartupRequest') @Watch('read') request: string = 'initial';
  @State status: string = 'Checking public SDK';
  private async read(): Promise<void> {
    const request = this.request;
    try {
      const supported = autoStartupManager.isAutoStartupSupported();
      let enabled = false;
      let code = 0;
      try { enabled = await autoStartupManager.getAutoStartupStatusForSelf(); }
      catch (error) { code = error.code; if (code !== 801) { throw new Error(`SDK startup status rejected ${error.code}: ${error.message}`); } }
      console.info(`NATIVEAPI_STARTUP_REFERENCE ${request} ${JSON.stringify({supported:supported,enabled:enabled,code:code})}`);
      const exportsValid = typeof nativeTests.sourceState === 'function' && typeof nativeTests.sourceBegin === 'function';
      if (!exportsValid) { throw new Error('Native source exports missing'); }
      const checked = await nativeTests.startupCheck(supported, enabled);
      let after = false;
      let afterCode = 0;
      try { after = await autoStartupManager.getAutoStartupStatusForSelf(); }
      catch (error) { afterCode = error.code; if (afterCode !== 801) { throw new Error(`SDK startup status rejected ${error.code}: ${error.message}`); } }
      if (after !== enabled || afterCode !== code) { throw new Error('Native checks modified the actual system status'); }
      this.status = checked;
      console.info(`NATIVEAPI_STARTUP_REPLY ${request} ${checked}`);
      console.info(`NATIVEAPI_STARTUP_SDK ${this.status}`);
    } catch (error) { console.error(`NATIVEAPI_STARTUP_SDK_ERROR ${error.code} ${error.message}`); }
  }
  aboutToAppear(): void { this.read(); }
  build() { Column({space:20}) { Text('NativeAPI Startup SDK Probe').fontSize(22)
    Text(this.status).fontSize(14)
    Button('Refresh SDK').id('startup-refresh').onClick(() => { this.read(); })
  }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF') }
}
