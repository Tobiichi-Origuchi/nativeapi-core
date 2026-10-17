import { deviceInfo } from '@kit.BasicServicesKit';
import settings from '@ohos.settings';
import nativeTests from 'libnativeapi_smoke.so';
interface Reference {
  name: string;
  settingsName: string;
  settingsError: number;
  marketName: string;
  model: string;
  manufacturer: string;
  osName: string;
  osVersion: string;
  osFullName: string;
  abiList: string;
}
@Entry
@Component
struct DeviceInfo {
  @StorageLink('nativeapiDeviceRequest') @Watch('read') request: string = 'initial';
  @StorageLink('nativeapiDeviceWriter') writer: boolean = false;
  @State result: string = 'Reading device information';
  private reference(): Reference {
    let name = '';
    let code = 0;
    try { name = settings.getValueSync(this.getUIContext().getHostContext(), settings.general.DEVICE_NAME, ''); }
    catch (e) { code = e.code; }
    return {name: name || deviceInfo.marketName, settingsName:name, settingsError:code,
      marketName:deviceInfo.marketName, model:deviceInfo.productModel, manufacturer:deviceInfo.manufacture,
      osName:deviceInfo.distributionOSName, osVersion:deviceInfo.distributionOSVersion || deviceInfo.osFullName,
      osFullName:deviceInfo.osFullName, abiList:deviceInfo.abiList};
  }
  private async read(): Promise<void> {
    if (this.writer) { this.result = 'Isolated SDK device-name writer'; return; }
    const request = this.request;
    try {
      const before = this.reference();
      console.info(`NATIVEAPI_DEVICE_REFERENCE ${request} ${JSON.stringify(before)}`);
      const checked = await nativeTests.deviceInfoCheck();
      if (JSON.stringify(before) !== JSON.stringify(this.reference())) throw new Error('Device SDK values changed during the read batch');
      this.result = checked;
      console.info(`NATIVEAPI_DEVICE_REPLY ${request} ${checked}`);
    } catch (e) { console.error(`NATIVEAPI_DEVICE_ERROR ${request} ${e.code} ${e.message}`); }
  }
  aboutToAppear(): void { this.read(); }
  build() { Column({space:20}) { Text('NativeAPI Device Information').fontSize(22)
    Text(this.result).fontSize(14)
  }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF') }
}
