import { window } from '@kit.ArkUI';
import { bundleManager, common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import nativeTests from 'libnativeapi_smoke.so';
import { WindowShadowFixture } from './WindowShadowFixture';

@Entry
@Component
struct WindowModeProbe {
  @State result: string = 'Probing public window mode support';
  private async pause(): Promise<void> { await new Promise<void>((resolve) => { setTimeout(resolve, 450); }); }
  private async run(): Promise<void> {
    try {
      const stage = WindowShadowFixture.stage;
      if (!stage) throw new Error('Missing fixture Stage');
      const main = stage.getMainWindowSync();
      const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
      const declared = context.abilityInfo.supportWindowModes;
      console.info(`NATIVEAPI_MODEPROBE_DECLARED ${JSON.stringify(declared)}`);
      if (main.isInFreeWindowMode() && main.getWindowStatus() !== window.WindowStatusType.FLOATING) await main.recover();
      const child = await stage.createSubWindowWithOptions('NativeAPI Mode SDK Child', {
        title: 'NativeAPI Mode SDK Child', decorEnabled: true, maximizeSupported: true
      });
      await child.setUIContent('pages/ControlsTarget');
      await child.resize(800, 500); await child.moveWindowTo(350, 500); await child.showWindow();
      const ordinary = await stage.createSubWindowWithOptions('NativeAPI Ordinary Mode Child', {title: 'NativeAPI Ordinary Mode Child', decorEnabled: true});
      await ordinary.setUIContent('pages/ControlsTarget'); await ordinary.resize(300, 180); await ordinary.moveWindowTo(750, 650); await ordinary.showWindow();
      const full = bundleManager.SupportWindowMode.FULL_SCREEN;
      const floating = bundleManager.SupportWindowMode.FLOATING;
      const split = bundleManager.SupportWindowMode.SPLIT;
      console.info(`NATIVEAPI_MODEPROBE_INPUT ${nativeTests.dragSessionProbe()}`);
      const phases = ['baseline', 'main-no-full', 'main-max-no-full', 'main-recover-no-full',
        'main-full-no-full', 'main-restore', 'main-full-restored', 'main-recover-restored',
        'child-no-full', 'child-max-no-full', 'child-recover-no-full', 'child-full-no-full',
        'child-restore', 'child-full-restored', 'child-recover-restored', 'child-layout-enter', 'child-layout-exit', 'ordinary-layout-enter', 'ordinary-layout-exit', 'ordinary-no-full', 'ordinary-native-full-refused', 'ordinary-full-modes', 'ordinary-native-full-enabled', 'ordinary-native-full-recover', 'main-floating-only', 'main-restore-final', 'ordinary-close', 'child-close'];
      for (let i = 0; i < phases.length; ++i) {
        const phase = phases[i]; let code = 0;
        try {
          if (phase === 'ordinary-no-full') await ordinary.setSupportedWindowModes([floating]);
          else if (phase === 'ordinary-native-full-refused' || phase === 'ordinary-native-full-enabled') await ordinary.maximize(window.MaximizePresentation.ENTER_IMMERSIVE);
          else if (phase === 'ordinary-full-modes') await ordinary.setSupportedWindowModes([full, floating]);
          else if (phase === 'ordinary-native-full-recover') await ordinary.recover();
          else if (phase === 'main-floating-only') await main.setSupportedWindowModes([floating]);
          else if (phase === 'main-restore-final') await main.setSupportedWindowModes(declared);
          else if (phase === 'main-no-full') await main.setSupportedWindowModes([floating, split]);
          else if (phase === 'main-restore') await main.setSupportedWindowModes([full, floating, split]);
          else if (phase === 'child-no-full') await child.setSupportedWindowModes([floating]);
          else if (phase === 'child-restore') await child.setSupportedWindowModes([full, floating]);
          else if (phase.endsWith('max-no-full')) await (phase.startsWith('main') ? main : child).maximize(window.MaximizePresentation.EXIT_IMMERSIVE);
          else if (phase === 'main-full-no-full' || phase === 'main-full-restored' || phase === 'child-full-no-full' || phase === 'child-full-restored') await (phase.startsWith('main') ? main : child).maximize(window.MaximizePresentation.ENTER_IMMERSIVE);
          else if (phase.includes('-recover-')) await (phase.startsWith('main') ? main : child).recover();
          else if (phase.includes('-layout-')) {
            const target = phase.startsWith('ordinary') ? ordinary : child;
            const enter = phase.endsWith('-enter');
            await target.setWindowLayoutFullScreen(enter);
            await target.setWindowSystemBarEnable(enter ? [] : ['status', 'navigation']);
          } else if (phase === 'ordinary-close') await ordinary.destroyWindow();
          else if (phase === 'child-close') await child.destroyWindow();
        } catch (error) { code = (error as BusinessError).code; }
        await this.pause();
        let childFull = false; let childLayout = false; let ordinaryFull = false; let ordinaryLayout = false; let ordinaryStatus = -1; let ordinaryRect: window.Rect | undefined = undefined;
        try { const p = child.getWindowProperties(); childFull = p.isFullScreen; childLayout = p.isLayoutFullScreen; } catch (_error) {}
        try { const p = ordinary.getWindowProperties(); ordinaryFull = p.isFullScreen; ordinaryLayout = p.isLayoutFullScreen; ordinaryRect = p.windowRect; ordinaryStatus = ordinary.getWindowStatus(); } catch (_error) {}
        let childStatus = -1; let childCode = 0; let childRect: window.Rect | undefined = undefined;
        try { childStatus = child.getWindowStatus(); childRect = child.getWindowProperties().windowRect; }
        catch (error) { childCode = (error as BusinessError).code; }
        console.info(`NATIVEAPI_MODEPROBE_PHASE ${i} ${phase} ${JSON.stringify({ code: code, declared: context.abilityInfo.supportWindowModes, mainStatus: main.getWindowStatus(), mainRect: main.getWindowProperties().windowRect, childStatus: childStatus, childCode: childCode, childRect: childRect, childFull: childFull, childLayout: childLayout, ordinaryFull: ordinaryFull, ordinaryLayout: ordinaryLayout, ordinaryRect: ordinaryRect, ordinaryStatus: ordinaryStatus, freeform: main.isInFreeWindowMode() })}`);
      }
      this.result = 'Public SDK mode probe complete';
      console.info('NATIVEAPI_MODEPROBE_DONE');
    } catch (error) { console.error(`NATIVEAPI_MODEPROBE_ERROR ${error.code} ${error.message}`); }
  }
  aboutToAppear(): void { setTimeout(() => { this.run(); }, 450); }
  build() { Column() { Text('NativeAPI SDK window mode reference'); Text(this.result) }.width('100%').height('100%').backgroundColor('#FFFFFF') }
}
