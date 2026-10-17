import fs from 'node:fs';
import path from 'node:path';
import { appTasks, OhosAppContext, OhosHapContext, OhosPluginId } from '@ohos/hvigor-ohos-plugin';
import { hvigor, HvigorNode } from '@ohos/hvigor';

// The companion host adapter is canonical library source, copied into this
// fixture module for ArkTS compilation. Never maintain a second handwritten copy.
const menuHostDirectory = path.join(__dirname, 'entry/src/main/ets/nativeapi');
fs.mkdirSync(menuHostDirectory, { recursive: true });
fs.copyFileSync(path.join(__dirname, '../../src/platform/ohos/ets/NativeMenuHost.ets'),
  path.join(menuHostDirectory, 'NativeMenuHost.ets'));
fs.copyFileSync(path.join(__dirname, '../../src/platform/ohos/ets/NativeShortcutHost.ets'),
  path.join(menuHostDirectory, 'NativeShortcutHost.ets'));

fs.copyFileSync(path.join(__dirname, '../../src/platform/ohos/ets/NativeTooltipHost.ets'),
  path.join(menuHostDirectory, 'NativeTooltipHost.ets'));

fs.copyFileSync(path.join(__dirname, '../../src/platform/ohos/ets/NativeWindowShadowHost.ets'),
  path.join(menuHostDirectory, 'NativeWindowShadowHost.ets'));

// Desktop-only apps keep their Ability foreground when their window is minimized.
// A mixed phone/desktop manifest instead follows the mobile Ability lifecycle.
// Change only the build model; never rewrite the source manifest in the mirror.
const denialFixture = process.env.NATIVEAPI_OHOS_NOTIFICATION_DENIAL === '1';
const keyboardFixture = process.env.NATIVEAPI_OHOS_KEYBOARD_ACL === '1';
const deviceWriter = process.env.NATIVEAPI_OHOS_DEVICEINFO_WRITER === '1';
if ([denialFixture, keyboardFixture, deviceWriter].filter(Boolean).length > 1) throw new Error('Fixture variants are mutually exclusive');
if (process.env.NATIVEAPI_OHOS_DESKTOP === '1' || denialFixture || keyboardFixture || deviceWriter) {
  hvigor.nodesEvaluated(() => {
    if (deviceWriter) {
      const app = hvigor.getRootNode().getContext(OhosPluginId.OHOS_APP_PLUGIN) as OhosAppContext;
      const manifest = app.getAppJsonOpt();
      manifest.app.bundleName = 'dev.nativeapi.deviceinfo.writer';
      manifest.app.label = '$string:app_device_writer_name';
      app.setAppJsonOpt(manifest);
    }
    if (keyboardFixture) {
      const app = hvigor.getRootNode().getContext(OhosPluginId.OHOS_APP_PLUGIN) as OhosAppContext;
      const manifest = app.getAppJsonOpt();
      manifest.app.bundleName = 'dev.nativeapi.keyboard.monitor';
      manifest.app.label = '$string:app_keyboard_name';
      app.setAppJsonOpt(manifest);
    }
    if (denialFixture) {
      const app = hvigor.getRootNode().getContext(OhosPluginId.OHOS_APP_PLUGIN) as OhosAppContext;
      const manifest = app.getAppJsonOpt();
      const bundleName = process.env.NATIVEAPI_OHOS_NOTIFICATION_DENIAL_BUNDLE ?? 'dev.nativeapi.notificationdenied';
      if (!/^dev\.nativeapi\.notificationdenied[0-9]*$/.test(bundleName)) {
        throw new Error('Use only the isolated notification-denial fixture namespace');
      }
      manifest.app.bundleName = bundleName;
      manifest.app.label = '$string:app_denial_name';
      app.setAppJsonOpt(manifest);
    }
    hvigor.getRootNode().subNodes((node: HvigorNode) => {
      const context = node.getContext(OhosPluginId.OHOS_HAP_PLUGIN) as OhosHapContext;
      if (context) {
        const manifest = context.getModuleJsonOpt();
        if (deviceWriter) {
          manifest.module.requestPermissions = [{name: 'ohos.permission.MANAGE_SETTINGS'}];
          manifest.module.abilities?.forEach((ability) => { ability.label = '$string:app_device_writer_name'; });
        }
        if (keyboardFixture) {
          manifest.module.requestPermissions = [{ name: 'ohos.permission.INPUT_MONITORING' }];
          manifest.module.abilities?.forEach((ability) => { ability.label = '$string:app_keyboard_name'; });
        }
        if (process.env.NATIVEAPI_OHOS_DESKTOP === '1') {
          manifest.module.deviceTypes = ['2in1'];
        }
        if (denialFixture) {
          manifest.module.abilities?.forEach((ability) => {
            ability.label = '$string:app_denial_name';
          });
        }
        context.setModuleJsonOpt(manifest);
      }
    });
  });
}
export default { system: appTasks };
