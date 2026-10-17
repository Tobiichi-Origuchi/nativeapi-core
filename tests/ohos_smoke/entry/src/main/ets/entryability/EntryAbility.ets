import { Configuration, UIAbility, Want } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import nativeTests from 'libnativeapi_smoke.so';
import { deviceNameWriter } from '../pages/DeviceNameWriter';
import { NativeWindowShadowHost } from '../nativeapi/NativeWindowShadowHost';
import { WindowShadowFixture } from '../pages/WindowShadowFixture';
import { runControlProbe } from '../pages/ControlProbe';
import { worker } from '@kit.ArkTS';
import lifetime from 'libnativeapi_clipboard_lifetime.so';
export default class EntryAbility extends UIAbility {
  private clipboardWorker?: worker.ThreadWorker;
  private clipboardLifetime(command: string, request: string): void {
    if (command === 'lifetime-finish') {
      console.info(`NATIVEAPI_CLIPBOARD_REPLY ${request} ${command} ${lifetime.finish()}`);
      return;
    }
    const instance = new worker.ThreadWorker('entry/ets/workers/ClipboardLifetime.ets', { name: 'NativeAPIClipboardLifetime' });
    this.clipboardWorker = instance;
    instance.onmessage = (): void => { instance.terminate(); };
    instance.onexit = (): void => {
      this.clipboardWorker = undefined;
      console.info(`NATIVEAPI_CLIPBOARD_REPLY ${request} ${command} ${lifetime.summary()}`);
    };
    instance.onerror = (event): void => { console.error(`NATIVEAPI_CLIPBOARD_ERROR lifetime worker ${event.message}`); };
    instance.postMessage(command === 'lifetime-pending' ? 'pending' : 'completed');
  }
  private closeStage?: window.WindowStage;
  private async nativeCloseCommand(command: string, request: string): Promise<void> {
    try {
      if (command === "create" && this.closeStage) {
        const child = await this.closeStage.createSubWindowWithOptions("NativeAPI Close Child", {title: "NativeAPI Close Child", decorEnabled: true});
        await child.setUIContent("pages/ControlsTarget");
        await child.resize(800, 500);
        await child.moveWindowTo(180, 300);
        nativeTests.nativeCloseAttach(child);
        await child.showWindow();
        console.info(`NATIVEAPI_NATIVE_CLOSE_CHILD ${JSON.stringify(child.getWindowProperties())}`);
      } else if (command === "destroy") {
        await window.findWindow("NativeAPI Close Child").destroyWindow();
      }
      nativeTests.nativeCloseState(command, request);
    } catch(error) { console.error(`NATIVEAPI_NATIVE_CLOSE_ERROR ${command} ${error}`); }
  }
  onCreate(want: Want): void {
    const suite = want.parameters?.['nativeapiSuite'];
    AppStorage.setOrCreate('nativeapiNativeCloseOnly', suite === 'native-close');
    AppStorage.setOrCreate('nativeapiSourceOnly', suite === 'drag-source');
    AppStorage.setOrCreate('nativeapiSourceScene', '');
    AppStorage.setOrCreate('nativeapiDropOnly', suite === 'drop-target');
    AppStorage.setOrCreate('nativeapiDropFiles', false);
    AppStorage.setOrCreate('nativeapiDropFilesOnly', false);
    AppStorage.setOrCreate('nativeapiKeyboardOnly', suite === 'keyboard-monitor');
    AppStorage.setOrCreate('nativeapiCustomShadowOnly', suite === 'window-shadow' || suite === 'window-shadow-main');
    AppStorage.setOrCreate('nativeapiMainShadowOnly', suite === 'window-shadow-main');
    AppStorage.setOrCreate('nativeapiShadowProbeOnly', suite === 'window-shadow-probe');
    AppStorage.setOrCreate('nativeapiShadowAction', '');
    AppStorage.setOrCreate('nativeapiFoundationOnly', suite === 'foundation');
    AppStorage.setOrCreate('nativeapiTooltipOnly', suite === 'tooltip');
    AppStorage.setOrCreate('nativeapiParentOnly', suite === 'window-parent');
    AppStorage.setOrCreate('nativeapiManagerOnly', suite === 'window-manager');
    AppStorage.setOrCreate('nativeapiPreferencesOnly', suite === 'preferences');
    AppStorage.setOrCreate('nativeapiPreferencesScope', want.parameters?.['nativeapiPreferencesScope'] || '');
    AppStorage.setOrCreate('nativeapiPreferencesCommand', 'initial');
    AppStorage.setOrCreate('nativeapiPreferencesRequest', 'initial');
    AppStorage.setOrCreate('nativeapiDeviceInfoOnly', suite === 'device-info' || suite === 'device-info-writer');
    AppStorage.setOrCreate('nativeapiDeviceWriter', this.context.applicationInfo.name === 'dev.nativeapi.deviceinfo.writer');
    AppStorage.setOrCreate('nativeapiDeviceRequest', 'initial');
    AppStorage.setOrCreate('nativeapiStartupOnly', suite === 'startup');
    AppStorage.setOrCreate('nativeapiStartupRequest', 'initial');
    AppStorage.setOrCreate('nativeapiShortcutsOnly', suite === 'shortcuts');
    AppStorage.setOrCreate('nativeapiLocalKeyHost', false);
    AppStorage.setOrCreate('nativeapiClipboardOnly', suite === 'clipboard' || suite === 'clipboard-boundaries');
    AppStorage.setOrCreate('nativeapiDisplaysOnly', suite === 'displays');
    AppStorage.setOrCreate('nativeapiCursorOnly', suite === 'cursor');
    const pickerStartUri = want.parameters?.['nativeapiPickerStartUri'];
    AppStorage.setOrCreate('nativeapiPickerStartUri', typeof pickerStartUri === 'string' ? pickerStartUri : '');
    AppStorage.setOrCreate('nativeapiPickerNoFilter', want.parameters?.['nativeapiPickerFilter'] === 'none');
    AppStorage.setOrCreate('nativeapiPickerAggregate', want.parameters?.['nativeapiPickerMerge'] === 'document');
    AppStorage.setOrCreate('nativeapiAspectOnly', suite === 'window-aspect');
    AppStorage.setOrCreate('nativeapiControlsOnly',suite==='window-controls');
    AppStorage.setOrCreate('nativeapiPoliciesOnly',suite==='window-policies');
    AppStorage.setOrCreate('nativeapiLayoutOnly',suite==='window-layout');
    AppStorage.setOrCreate('nativeapiLayoutAck','');
    AppStorage.setOrCreate('nativeapiFullscreenOnly',suite==='window-fullscreen');
    AppStorage.setOrCreate('nativeapiFullscreenAck','');
    AppStorage.setOrCreate('nativeapiModePolicyOnly',suite==='window-mode-policy');
    AppStorage.setOrCreate('nativeapiModeAck','');
    AppStorage.setOrCreate('nativeapiModeProbeOnly',suite==='window-mode-probe');
    AppStorage.setOrCreate('nativeapiTitleProbeOnly',suite==='window-title-probe');
    AppStorage.setOrCreate('nativeapiTitleOnly',suite==='window-title');
    AppStorage.setOrCreate('nativeapiTitleArmed',false);
    AppStorage.setOrCreate('nativeapiTitleTarget',0);
    AppStorage.setOrCreate('nativeapiTitleSerial',0);
    AppStorage.setOrCreate('nativeapiTitleCompleted',0);
    AppStorage.setOrCreate('nativeapiTitleUseC',false);
    AppStorage.setOrCreate('nativeapiTitleWorker',false);
    AppStorage.setOrCreate('nativeapiTitleExpected',false);
    AppStorage.setOrCreate('nativeapiTitleOverlap',false);
    AppStorage.setOrCreate('nativeapiTitleAck','');
    AppStorage.setOrCreate('nativeapiPolicyAction','');
    AppStorage.setOrCreate('nativeapiControlProbeOnly',suite==='control-probe');
    AppStorage.setOrCreate('nativeapiWindowDragOnly',suite === 'window-drag');
    AppStorage.setOrCreate('nativeapiWindowShapeOnly', suite === 'window-shape');
    AppStorage.setOrCreate('nativeapiWindowStyleOnly', suite === 'window-style');
    AppStorage.setOrCreate('nativeapiOcclusionOnly', suite === 'occlusion');
    AppStorage.setOrCreate('nativeapiAppIntegrations', suite === 'integrations');
    AppStorage.setOrCreate('nativeapiTrayOnly', suite === 'tray');
    AppStorage.setOrCreate('nativeapiTrayReference', suite === 'tray-reference');
    AppStorage.setOrCreate('nativeapiApplicationOnly', suite === 'application');
    AppStorage.setOrCreate('nativeapiAppColorMode', this.context.config.colorMode ?? -1);
    AppStorage.setOrCreate('nativeapiViewsOnly', suite === 'views');
    AppStorage.setOrCreate('nativeapiMenusOnly', suite === 'menus');
    AppStorage.setOrCreate('nativeapiAccessibilityOnly', suite === 'accessibility');
    AppStorage.setOrCreate('nativeapiAccessibilityRun', String(want.parameters?.['nativeapiAccessRun'] ?? 'boot'));
    const argument = want.parameters?.['nativeapi.notification.argument'];
    let notificationMode = '';
    if (suite === 'notification') { notificationMode = 'normal'; }
    if (suite === 'notification-prepare-open') { notificationMode = 'prepare-open'; }
    if (suite === 'notification-prepare-button') { notificationMode = 'prepare-button'; }
    if (suite === 'notification-denied') { notificationMode = 'denied'; }
    if (argument === 'tag=cold_open&action=open') { notificationMode = 'recover-open'; }
    if (argument === 'tag=cold_button&action=button') { notificationMode = 'recover-button'; }
    AppStorage.setOrCreate('nativeapiNotificationOnly', notificationMode.length > 0);
    AppStorage.setOrCreate('nativeapiNotificationMode', notificationMode);
    AppStorage.setOrCreate('nativeapiUrlOnly', want.parameters?.['nativeapiSuite'] === 'url');
    const link = want.parameters?.['nativeapiUrl'];
    AppStorage.setOrCreate('nativeapiUrl', typeof link === 'string' ? link : 'https://example.com');
    AppStorage.setOrCreate('nativeapiUrlUseC', want.parameters?.['nativeapiUrlBinding'] === 'c');
    AppStorage.setOrCreate('nativeapiPickerReference', want.parameters?.['nativeapiSuite'] === 'reference' ||
      want.parameters?.['nativeapiSuite'] === 'reference-folder');
    AppStorage.setOrCreate('nativeapiPickerReferenceFolder', want.parameters?.['nativeapiSuite'] === 'reference-folder');
    AppStorage.setOrCreate('nativeapiPickerCancelOnly', want.parameters?.['nativeapiSuite'] === 'picker-cancel');
    AppStorage.setOrCreate('nativeapiPickerOnly', want.parameters?.['nativeapiSuite'] === 'picker' ||
      want.parameters?.['nativeapiSuite'] === 'picker-cancel');
    if (!nativeTests.attachContext(this.context)) {
      console.error('NativeAPI UIAbilityContext attachment failed');
    }
    nativeTests.applicationLifecycle(this.context, 'create');
    nativeTests.handleNotificationWant(want, true);
    deviceNameWriter(this.context, want);
  }
  onForeground(): void { nativeTests.applicationLifecycle(this.context, 'foreground'); }
  onBackground(): void { nativeTests.applicationLifecycle(this.context, 'background'); }
  onDestroy(): void {
    WindowShadowFixture.host?.dispose(); WindowShadowFixture.host=undefined; WindowShadowFixture.stage=undefined;
    nativeTests.applicationLifecycle(this.context, 'destroy');
  }
  onConfigurationUpdate(config: Configuration): void {
    AppStorage.setOrCreate('nativeapiAppColorMode', config.colorMode ?? -1);
    console.info(`NATIVEAPI_APP_CONFIG colorMode=${config.colorMode}`);
  }
  onNewWant(want: Want): void {
    if (AppStorage.get<boolean>('nativeapiPreferencesOnly') && want.parameters?.['nativeapiSuite'] === 'preferences') {
      const scope = want.parameters?.['nativeapiPreferencesScope'];
      const command = want.parameters?.['nativeapiPreferencesCommand'];
      const request = want.parameters?.['nativeapiPreferencesRequest'];
      if (typeof scope === 'string' && typeof command === 'string' && typeof request === 'string') {
        AppStorage.set('nativeapiPreferencesScope', scope);
        AppStorage.set('nativeapiPreferencesCommand', command);
        AppStorage.set('nativeapiPreferencesRequest', request);
      }
    }
    deviceNameWriter(this.context, want);
    if (AppStorage.get<boolean>('nativeapiDeviceInfoOnly') && want.parameters?.['nativeapiSuite'] === 'device-info') {
      const request = want.parameters?.['nativeapiDeviceRequest'];
      if (typeof request === 'string') AppStorage.set('nativeapiDeviceRequest', request);
    }
    if (AppStorage.get<boolean>('nativeapiKeyboardOnly') && want.parameters?.['nativeapiSuite'] === 'keyboard-monitor') {
      const command = want.parameters?.['nativeapiKeyboardCommand'];
      const request = want.parameters?.['nativeapiKeyboardRequest'];
      if (typeof command === 'string' && typeof request === 'string' &&
          ['setup', 'clear', 'state', 'stop-a', 'start-a', 'stop-all', 'worker-stop', 'worker-start', 'hold', 'replace-drain', 'self-stop', 'self-delete', 'self-free', 'remove-a', 'listen-a', 'finish', 'finish-check'].includes(command)) {
        nativeTests.keyboardState(command, request);
      }
    }
    if (AppStorage.get<boolean>('nativeapiStartupOnly') && want.parameters?.['nativeapiSuite'] === 'startup') {
      const request = want.parameters?.['nativeapiStartupRequest'];
      if (typeof request === 'string') AppStorage.set('nativeapiStartupRequest', request);
    }
    if (AppStorage.get<boolean>("nativeapiNativeCloseOnly") && want.parameters?.["nativeapiSuite"] === "native-close") {
      const command = want.parameters?.["nativeapiNativeCloseCommand"];
      const request = want.parameters?.["nativeapiNativeCloseRequest"];
      if (typeof command === "string" && typeof request === "string") { this.nativeCloseCommand(command, request); }
    }
    if (AppStorage.get<boolean>('nativeapiSourceOnly') && want.parameters?.['nativeapiSuite'] === 'drag-source') {
      const command = want.parameters?.['nativeapiSourceCommand'];
      const request = want.parameters?.['nativeapiSourceRequest'];
      if (typeof command === 'string' && typeof request === 'string') {
        if (command.startsWith('cpp-') || command.startsWith('c-') || command === 'sdk-reference') { AppStorage.set('nativeapiSourceScene', command); }
        nativeTests.sourceState(command, request);
      }
    }
    if (AppStorage.get<boolean>('nativeapiDropOnly') && want.parameters?.['nativeapiSuite'] === 'drop-target') {
      const command = want.parameters?.['nativeapiDropCommand'];
      const request = want.parameters?.['nativeapiDropRequest'];
      if (typeof command === 'string' && typeof request === 'string' &&
          ['setup', 'listen', 'unlisten', 'state', 'copy', 'none', 'move', 'link', 'self-remove', 'c-listen', 'c-remove', 'files', 'files-only', 'text'].includes(command)) {
        if (command === 'files' || command === 'files-only' || command === 'text') {
          AppStorage.set('nativeapiDropFiles', command !== 'text');
          AppStorage.set('nativeapiDropFilesOnly', command === 'files-only');
        }
        nativeTests.dropState(command, request);
      }
    }
    if (AppStorage.get<boolean>('nativeapiShortcutsOnly') && want.parameters?.['nativeapiSuite'] === 'shortcuts') {
      const command = want.parameters?.['nativeapiShortcutCommand'];
      const request = want.parameters?.['nativeapiShortcutRequest'];
      if (typeof command === 'string' && typeof request === 'string' &&
          ['setup', 'register', 'worker', 'release', 'conflict', 'clear', 'finish', 'state', 'disable-manager', 'enable-manager', 'disable-shortcut', 'enable-shortcut', 'self-remove', 'input-reference', 'local-register', 'local-register-c', 'local-worker', 'local-release', 'local-host-attach', 'local-host-detach', 'local-event-remove'].includes(command)) {
        if (command === 'local-host-attach' || command === 'local-host-detach') {
          AppStorage.set('nativeapiLocalKeyHost', command === 'local-host-attach');
          setTimeout(() => { nativeTests.shortcutState(command, request); }, 100);
        } else nativeTests.shortcutState(command, request);
      }
    }
    if (AppStorage.get<boolean>('nativeapiClipboardOnly') && want.parameters?.['nativeapiSuite'] === 'clipboard') {
      const command = want.parameters?.['nativeapiClipboardCommand'];
      const request = want.parameters?.['nativeapiClipboardRequest'];
      if (typeof command === 'string' && typeof request === 'string' &&
          ['lifetime-pending', 'lifetime-completed', 'lifetime-finish'].includes(command)) {
        this.clipboardLifetime(command, request);
      }
      if (typeof command === 'string' && typeof request === 'string' && command.startsWith('boundary-')) {
        nativeTests.clipboardBoundary(command, request);
      }
      if (typeof command === 'string' && typeof request === 'string' &&
          ['listen', 'unlisten', 'publish', 'publish-c', 'validate-paths', 'read', 'read-c', 'read-worker', 'files', 'state'].includes(command)) {
        nativeTests.clipboardInterop(command, request);
      }
    }
    if (AppStorage.get<boolean>('nativeapiDisplaysOnly') && want.parameters?.['nativeapiSuite'] === 'displays') {
      const command = want.parameters?.['nativeapiDisplayCommand'];
      const request = want.parameters?.['nativeapiDisplayRequest'];
      if (typeof command === 'string' && ['verify', 'unlisten', 'listen', 'listen-worker', 'unlisten-worker',
        'remove-c', 'hold-queue', 'release-queue', 'replace-queue', 'finish', 'fullscreen', 'windowed'].includes(command)) {
        nativeTests.displayState(command, typeof request === 'string' ? request : '');
      }
    }
    if (want.parameters?.['nativeapiSuite'] === 'accessibility') {
      AppStorage.set('nativeapiAccessibilityRun', String(want.parameters?.['nativeapiAccessRun'] ?? Date.now()));
    }
    if (want.parameters?.['nativeapiSuite'] === 'tooltip' && want.parameters?.['nativeapiTooltipAction']) {
      AppStorage.setOrCreate('nativeapiTooltipAction', String(want.parameters['nativeapiTooltipAction']));
    }
    if(['window-shadow-probe','window-shadow','window-shadow-main'].includes(String(want.parameters?.['nativeapiSuite'])) && want.parameters?.['nativeapiShadowAction']) {
      AppStorage.setOrCreate('nativeapiShadowAction', String(want.parameters['nativeapiShadowAction']));
    }
    nativeTests.handleNotificationWant(want, false);
    if(want.parameters?.['nativeapiSuite']==='window-mode-policy'&&want.parameters?.['nativeapiModeAck']) {
      AppStorage.set('nativeapiModeAck',String(want.parameters['nativeapiModeAck']));
    }
    if(want.parameters?.['nativeapiSuite']==='window-layout'&&want.parameters?.['nativeapiLayoutAck']) {
      AppStorage.setOrCreate('nativeapiLayoutAck',String(want.parameters['nativeapiLayoutAck']));
    }
    if(want.parameters?.['nativeapiSuite']==='window-fullscreen'&&want.parameters?.['nativeapiFullscreenAck']) {
      AppStorage.set('nativeapiFullscreenAck',String(want.parameters['nativeapiFullscreenAck']));
    }
    if(want.parameters?.['nativeapiSuite']==='window-title'&&want.parameters?.['nativeapiTitleAck']) {
      AppStorage.set('nativeapiTitleAck',String(want.parameters['nativeapiTitleAck']));
    }
    if(want.parameters?.['nativeapiSuite']==='window-policies'&&want.parameters?.['nativeapiPolicyAction']) {
      const command=String(want.parameters['nativeapiPolicyAction']);
      AppStorage.setOrCreate('nativeapiPolicyAction',command);
    }
  }
  onWindowStageCreate(stage: window.WindowStage): void {
    this.closeStage = stage;
    WindowShadowFixture.stage = stage;
    AppStorage.setOrCreate('nativeapiShortcutWindowName', stage.getMainWindowSync().getWindowProperties().name);
    if (!nativeTests.attachWindowStage(stage)) {
      console.error('NativeAPI WindowStage attachment failed');
    }
    if(AppStorage.get<boolean>('nativeapiCustomShadowOnly')) {
      const host=new NativeWindowShadowHost(stage,'pages/NativeShadowSurface');
      WindowShadowFixture.host=host;
      if(!nativeTests.attachShadowHost(host))throw new Error('Custom shadow host attach failed');
    }
    stage.loadContent(AppStorage.get<boolean>('nativeapiMainShadowOnly') ? 'pages/WindowShadowMain' :
      AppStorage.get<boolean>('nativeapiCustomShadowOnly') ? 'pages/WindowShadow' :
      AppStorage.get<boolean>('nativeapiShadowProbeOnly') ? 'pages/WindowShadowProbe' :
      AppStorage.get<boolean>('nativeapiFoundationOnly') ? 'pages/Foundation' :
      AppStorage.get<boolean>('nativeapiTooltipOnly') ? 'pages/Tooltip' :
      AppStorage.get<boolean>('nativeapiParentOnly') ? 'pages/WindowParent' :
      AppStorage.get<boolean>('nativeapiManagerOnly') ? 'pages/WindowManager' :
      AppStorage.get<boolean>('nativeapiPreferencesOnly') ? 'pages/Preferences' :
      AppStorage.get<boolean>('nativeapiDeviceInfoOnly') ? 'pages/DeviceInfo' :
      AppStorage.get<boolean>('nativeapiKeyboardOnly') ? 'pages/KeyboardMonitor' :
      AppStorage.get<boolean>('nativeapiStartupOnly') ? 'pages/Startup' :
      AppStorage.get<boolean>('nativeapiNativeCloseOnly') ? 'pages/NativeClose' :
      AppStorage.get<boolean>('nativeapiSourceOnly') ? 'pages/DragSource' :
      AppStorage.get<boolean>('nativeapiDropOnly') ? 'pages/DropTarget' :
      AppStorage.get<boolean>('nativeapiShortcutsOnly') ? 'pages/Shortcuts' :
      AppStorage.get<boolean>('nativeapiClipboardOnly') ? 'pages/ClipboardInterop' :
      AppStorage.get<boolean>('nativeapiDisplaysOnly') ? 'pages/Displays' :
      AppStorage.get<boolean>('nativeapiCursorOnly') ? 'pages/Cursor' :
      AppStorage.get<boolean>('nativeapiAspectOnly') ? 'pages/WindowAspect' :
      AppStorage.get<boolean>('nativeapiControlsOnly') ? 'pages/WindowControls' :
      AppStorage.get<boolean>('nativeapiPoliciesOnly') ? 'pages/WindowPolicies' :
      AppStorage.get<boolean>('nativeapiLayoutOnly') ? 'pages/WindowLayout' :
      AppStorage.get<boolean>('nativeapiFullscreenOnly') ? 'pages/WindowFullscreen' :
      AppStorage.get<boolean>('nativeapiModePolicyOnly') ? 'pages/WindowModePolicy' :
      AppStorage.get<boolean>('nativeapiModeProbeOnly') ? 'pages/WindowModeProbe' :
      AppStorage.get<boolean>('nativeapiTitleProbeOnly') ? 'pages/WindowTitleProbe' :
      AppStorage.get<boolean>('nativeapiTitleOnly') ? 'pages/WindowTitle' :
      AppStorage.get<boolean>('nativeapiControlProbeOnly') ? 'pages/ControlProbe' :
      AppStorage.get<boolean>('nativeapiWindowDragOnly') ? 'pages/WindowDrag' :
      AppStorage.get<boolean>('nativeapiWindowShapeOnly') ? 'pages/WindowShape' :
      AppStorage.get<boolean>('nativeapiWindowStyleOnly') ? 'pages/WindowStyle' :
      AppStorage.get<boolean>('nativeapiOcclusionOnly') ? 'pages/Occlusion' :
      AppStorage.get<boolean>('nativeapiAppIntegrations') ? 'pages/Integrations' :
      AppStorage.get<boolean>('nativeapiTrayOnly') ? 'pages/Tray' :
      AppStorage.get<boolean>('nativeapiTrayReference') ? 'pages/TrayReference' :
      AppStorage.get<boolean>('nativeapiApplicationOnly') ? 'pages/Application' :
      AppStorage.get<boolean>('nativeapiAccessibilityOnly') ? 'pages/Accessibility' :
      AppStorage.get<boolean>('nativeapiMenusOnly') ? 'pages/Menus' :
      AppStorage.get<boolean>('nativeapiViewsOnly') ? 'pages/Views' : 'pages/Index', (error) => {
      if (error.code) {
        console.error(`NativeAPI test page failed: ${error.code}`);
      } else if (AppStorage.get<boolean>('nativeapiControlProbeOnly')) {
        runControlProbe(stage);
      }
    });
  }
}
