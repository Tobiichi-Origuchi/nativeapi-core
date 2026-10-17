import { preferences } from '@kit.ArkData';
import nativeTests from 'libnativeapi_smoke.so';

@Entry
@Component
struct PreferencesTest {
  @StorageLink('nativeapiPreferencesRequest') @Watch('run') request: string = 'initial';
  @StorageLink('nativeapiPreferencesCommand') command: string = 'initial';
  @StorageLink('nativeapiPreferencesScope') scope: string = '';
  @State result: string = 'Preferences fixture ready';
  private async run(): Promise<void> {
    const request = this.request;
    const command = this.command;
    if (command === 'initial') { console.info('NATIVEAPI_PREFERENCES_READY'); return; }
    try {
      if (!/^nativeapi-pref-test-[a-z0-9-]+$/.test(this.scope) || this.scope.length >= 70) throw new Error('Invalid disposable scope');
      let name = 'nativeapi_';
      for (let i = 0; i < this.scope.length; ++i) name += this.scope.charCodeAt(i).toString(16).padStart(2, '0');
      const host = this.getUIContext().getHostContext();
      if (!host) throw new Error('Missing owning host context');
      const context = host.getApplicationContext();
      let native = '';
      if (command !== 'sdk-write') native = await nativeTests.preferencesCommand(command, this.scope);
      // Obtain a fresh independent SDK view of the actual persisted file.
      preferences.removePreferencesFromCacheSync(context, name);
      const sdk = preferences.getPreferencesSync(context, {name: name, storageType: preferences.StorageType.GSKV});
      if (command === 'sdk-write') {
        sdk.putSync('sdk', 'SDK→native 中文🙂');
        sdk.flushSync();
      }
      const reply = JSON.stringify({native: native, sdk: sdk.getAllSync(),
        maxKey: preferences.MAX_KEY_LENGTH, maxValue: preferences.MAX_VALUE_LENGTH});
      this.result = reply;
      console.info(`NATIVEAPI_PREFERENCES_REPLY ${request} ${command} ${reply}`);
    } catch (error) { console.error(`NATIVEAPI_PREFERENCES_ERROR ${request} ${command} ${error.code} ${error.message}`); }
  }
  aboutToAppear(): void { this.run(); }
  build() { Column({space:20}) {
    Text('NativeAPI Preferences').fontSize(22)
    Text(this.result).fontSize(14)
  }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF') }
}
