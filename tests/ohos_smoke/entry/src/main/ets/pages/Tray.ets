import nativeTests from 'libnativeapi_smoke.so';
interface TrayState {
  supported: boolean; visible: boolean; replaced: boolean; template: boolean; tooltip: boolean; burst: boolean; workerTooltip: boolean;
  passed: number; failed: number; clicks: number; cClicks: number; menuClicks: number;
}
@Entry
@Component
struct Tray {
  @State result: string = 'NativeAPI tray tests starting';
  @State busy: boolean = true;
  private invoke(command: string): TrayState {
    this.result = nativeTests.runTray(command);
    return JSON.parse(this.result) as TrayState;
  }
  private async wait(property: string, expected: boolean): Promise<void> {
    for (let i = 0; i < 100; i++) {
      await new Promise<void>((resolve) => { setTimeout(resolve, 100); });
      const state = this.invoke('status');
      const actual = property === 'visible' ? state.visible : property === 'replaced' ? state.replaced :
        property === 'template' ? state.template : property === 'burst' ? state.burst :
        property === 'worker' ? state.workerTooltip : state.tooltip;
      if (actual === expected) { return; }
    }
    console.error(`NATIVEAPI_TRAY_TIMEOUT ${property}`);
    throw new Error('Native tray completion timeout');
  }
  private async action(command: string, property: string, expected: boolean, check: string): Promise<void> {
    if (this.busy) { return; }
    this.busy = true;
    try {
      this.invoke(command);
      if (property.length > 0) { await this.wait(property, expected); }
      else { await new Promise<void>((resolve) => { setTimeout(resolve, 1500); }); }
      this.invoke(check);
      console.info(`NATIVEAPI_TRAY_UI_DONE ${command} ${this.result}`);
    } catch (error) { this.result = `NATIVEAPI_TRAY_ERROR ${error}`; console.error(this.result); }
    this.busy = false;
  }
  aboutToAppear(): void {
    setTimeout(async () => {
      try {
        const state = this.invoke('setup');
        if (state.supported) {
          await this.wait('visible', true); this.invoke('shown');
          await nativeTests.runTrayWorker(); await this.wait('worker', true);
          console.info(`NATIVEAPI_TRAY_READY ${this.result}`);
        } else { await nativeTests.runTrayWorker(); await this.wait('worker', true); this.invoke('finish'); }
      } catch (error) { this.result = `NATIVEAPI_TRAY_ERROR ${error}`; console.error(this.result); }
      this.busy = false;
    }, 800);
  }
  build() {
    Column({ space: 8 }) {
      Text('NativeAPI Tray Acceptance').fontSize(24)
      Text(this.result).fontSize(15)
      Button('NativeAPI Update tray').id('nativeapi-tray-update').enabled(!this.busy)
        .onClick(() => { this.action('update', 'replaced', true, 'updated'); })
      Button('NativeAPI Template tray').id('nativeapi-tray-template').enabled(!this.busy)
        .onClick(() => { this.action('template', 'template', true, 'templated'); })
      Button('NativeAPI Restore tray').id('nativeapi-tray-restore').enabled(!this.busy)
        .onClick(() => { this.action('restore', 'template', false, 'restored'); })
      Button('NativeAPI Tooltip tray').id('nativeapi-tray-tooltip').enabled(!this.busy)
        .onClick(() => { this.action('tooltip', 'tooltip', true, 'tooltipped'); })
      Button('NativeAPI Bad tooltip').id('nativeapi-tray-bad-tooltip').enabled(!this.busy)
        .onClick(() => { this.action('bad-tooltip', '', false, 'bad-tooltipped'); })
      Button('NativeAPI Burst tray').id('nativeapi-tray-burst').enabled(!this.busy)
        .onClick(() => { this.action('burst', 'burst', true, 'burst-done'); })
      Button('NativeAPI Rename tray menu').id('nativeapi-tray-menu').enabled(!this.busy)
        .onClick(() => { this.action('rename-menu', '', false, 'status'); })
      Button('NativeAPI Hide tray').id('nativeapi-tray-hide').enabled(!this.busy)
        .onClick(() => { this.action('hide', 'visible', false, 'hidden'); })
      Button('NativeAPI Show tray').id('nativeapi-tray-show').enabled(!this.busy)
        .onClick(() => { this.action('show', 'visible', true, 'shown'); })
      Button('NativeAPI Dispose tray').id('nativeapi-tray-dispose').enabled(!this.busy)
        .onClick(() => { this.action('dispose', '', false, 'finish'); })
    }.padding(24).width('100%').height('100%')
  }
}
