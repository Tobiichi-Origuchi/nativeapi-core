import { common, Want } from '@kit.AbilityKit';
import { preferences } from '@kit.ArkData';
import settings from '@ohos.settings';

// Only the separately signed, isolated fixture can mutate this setting. The
// ordinary reader does not declare MANAGE_SETTINGS. Original state is persisted
// before mutation so a restarted writer can still restore it.
export function deviceNameWriter(context: common.UIAbilityContext, want: Want): void {
  if (want.parameters?.['nativeapiSuite'] !== 'device-info-writer') return;
  const request = want.parameters?.['nativeapiDeviceRequest'];
  const command = want.parameters?.['nativeapiDeviceCommand'];
  if (typeof request !== 'string' || typeof command !== 'string') return;
  try {
    if (context.applicationInfo.name !== 'dev.nativeapi.deviceinfo.writer') throw new Error('Wrong writer bundle');
    if (!['prepare', 'set-a', 'set-b', 'restore'].includes(command)) throw new Error('Invalid writer command');
    const key = settings.general.DEVICE_NAME;
    const store = preferences.getPreferencesSync(context, {name: 'nativeapi_device_name_original'});
    const read = (): string => settings.getValueSync(context, key, '');
    const a = 'NativeAPI·设备甲🙂';
    const b = 'NativeAPI·Device-B-é';
    const restore = (): void => {
      if (!store.getSync('active', false)) return;
      const original = store.getSync('original', '');
      if (typeof original !== 'string') throw new Error('Invalid stored original');
      const current = read();
      if (current !== a && current !== b && current !== original) throw new Error('External device-name change; refusing to overwrite it');
      if (!settings.setValueSync(context, key, original) || read() !== original) throw new Error('Actual SDK restoration failed');
      store.putSync('active', false);
      store.flushSync();
    };
    if (command === 'prepare') {
      restore();
      store.putSync('original', read());
      store.putSync('active', true);
      store.flushSync();
    } else if (command === 'restore') {
      restore();
    } else {
      if (store.getSync('active', false) !== true) throw new Error('Capture original before changing name');
      const value = command === 'set-a' ? a : b;
      if (!settings.setValueSync(context, key, value) || read() !== value) throw new Error('Actual SDK device-name update failed');
    }
    console.info(`NATIVEAPI_DEVICE_WRITER_REPLY ${request} ${command} ${JSON.stringify({name:read(),original:store.getSync('original',''),active:store.getSync('active',false)})}`);
  } catch (error) { console.error(`NATIVEAPI_DEVICE_WRITER_ERROR ${request} ${command} ${error.code} ${error.message}`); }
}
