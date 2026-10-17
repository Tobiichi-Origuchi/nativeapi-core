import { window } from '@kit.ArkUI';
import { common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import nativeTests from 'libnativeapi_smoke.so';
interface ParentState { ready: boolean; system: number[]; parents: number[]; alive: number[]; }
interface ParentReply { passed: number; failed: number; result: boolean; second: boolean; parents: number[]; }
@Entry
@Component
struct WindowParentTest {
  @State result: string = 'Preparing native window parents';
  private windows: window.Window[] = [];
  private total: number = 0;
  private phases: number = 0;
  private async pause(): Promise<void> { await new Promise<void>((resolve) => { setTimeout(resolve, 200); }); }
  private state(): ParentState { return JSON.parse(nativeTests.parentState('')) as ParentState; }
  private sdkParents(): number[] {
    return this.windows.map((w: window.Window, i: number) => {
      if (i === 0) return 0;
      try { return w.getParentWindow().getWindowProperties().id; } catch (_) { return 0; }
    });
  }
  private async phase(name: string, action: string = 'query', receiver: number = 3, target: number = 0,
    useC: boolean = false, worker: boolean = false, expected?: boolean): Promise<void> {
    const reply = JSON.parse(await nativeTests.parentCommand(action, receiver, target, useC, worker)) as ParentReply;
    if (reply.failed !== 0 || expected !== undefined && reply.result !== expected) throw new Error(`Native request mismatch ${name}: ${JSON.stringify(reply)}`);
    if (action === 'pending' && reply.second) throw new Error('Overlapping parent change was accepted');
    await this.pause();
    const observed = JSON.parse(await nativeTests.parentCommand('query', 3, 0, true, true)) as ParentReply;
    const sdk = this.sdkParents();
    const live = this.state();
    if (observed.failed !== 0 || JSON.stringify(observed.parents) !== JSON.stringify(sdk) || JSON.stringify(live.parents) !== JSON.stringify(sdk)) {
      throw new Error(`Native/SDK parents differ ${name} ${JSON.stringify({observed:observed, sdk:sdk, live:live})}`);
    }
    this.total += reply.passed + observed.passed; this.phases++;
    console.info(`NATIVEAPI_PARENT_PHASE ${name} ${JSON.stringify({native:reply, observed:observed, sdk:sdk, live:live, worker:worker, useC:useC})}`);
    this.result = `${name}: ${JSON.stringify(sdk)}`;
  }
  private async run(): Promise<void> {
    try {
      const main = await window.getLastWindow(this.getUIContext().getHostContext() as common.UIAbilityContext);
      nativeTests.parentState('setup');
      for (let i = 0; i < 100 && !this.state().ready; ++i) await this.pause();
      if (!this.state().ready) throw new Error('Native children did not attach');
      this.windows = [main, window.findWindow('NativeAPI Parent 1'), window.findWindow('NativeAPI Parent 2'), window.findWindow('NativeAPI Parent 3')];
      const a = this.windows[1], b = this.windows[2], child = this.windows[3];
      let code = 0;
      try { await child.setParentWindow(a.getWindowProperties().id); }
      catch (error) { code = (error as BusinessError).code; }
      if (code !== 0 && code !== 801) throw new Error(`Unexpected reference reparent code ${code}`);
      const supported = code === 0;
      if (supported) {
        if (child.getParentWindow().getWindowProperties().id !== a.getWindowProperties().id) throw new Error('SDK reference did not establish its parent');
        await child.setParentWindow(main.getWindowProperties().id);
      }
      let sameParentCode = 0;
      try { await child.setParentWindow(main.getWindowProperties().id); }
      catch (error) { sameParentCode = (error as BusinessError).code; }
      if (sameParentCode !== 0 && sameParentCode !== 1300009 && sameParentCode !== 801) throw new Error(`Unexpected same-parent SDK code ${sameParentCode}`);
      console.info(`NATIVEAPI_PARENT_REFERENCE ${JSON.stringify({code:code, sameParentCode:sameParentCode, system:this.state().system, sdk:this.sdkParents()})}`);
      await this.phase('initial');
      await this.phase('invalid-ui', 'invalid');
      await this.phase('invalid-worker', 'invalid', 3, 0, true, true);
      await this.phase('ui-to-a', 'set', 3, 1, false, false);
      if (supported && this.sdkParents()[3] !== a.getWindowProperties().id) throw new Error('UI reparent did not finish');
      await this.phase('cycle', 'set', 1, 3, false, true, false);
      await this.phase('c-worker-to-b', 'set', 3, 2, true, true, supported);
      if (supported && this.sdkParents()[3] !== b.getWindowProperties().id) throw new Error('C worker reparent did not finish');
      if (supported) await child.setParentWindow(a.getWindowProperties().id);
      await this.phase('external-to-a');
      await this.phase('external-cycle', 'set', 1, 3, true, true, false);
      await this.phase('worker-to-main', 'set', 3, 0, false, true, supported);
      await this.phase('c-ui-to-main', 'set', 3, 0, true, false);
      await this.phase('same-parent', 'set', 3, 0, false, true, sameParentCode === 0);
      await this.phase('pending', 'pending', 3, 1, false, false, supported ? true : undefined);
      await this.phase('retry-after-pending', 'set', 3, 2, true, true, supported);
      await a.destroyWindow(); await this.pause();
      await this.phase('old-parent-destroyed');
      if (supported && this.state().alive[3] !== 1) throw new Error('Destroying old parent destroyed reparented child');
      await this.phase('dead-parent', 'set', 3, 1, false, true, false);
      await b.destroyWindow(); await this.pause();
      await this.phase('active-parent-destroyed');
      if (this.state().alive[3]) await child.destroyWindow();
      await this.pause();
      await this.phase('closed-child', 'set', 3, 0, true, true, false);
      if (this.state().alive.slice(1).some((value: number) => value !== 0)) throw new Error('Native child registry survived actual destruction');
      console.info(`NATIVEAPI_PARENT_DONE ${JSON.stringify({passed:this.total, failed:0, phases:this.phases, sdkCode:code})}`);
    } catch (error) { console.error(`NATIVEAPI_PARENT_ERROR ${error.code} ${error.message}`); }
  }
  aboutToAppear(): void { setTimeout(() => { this.run(); }, 400); }
  build() { Column({space:20}) { Text('NativeAPI Window Parent Acceptance').fontSize(22)
    Text(this.result).fontSize(14)
  }.width('100%').height('100%').padding(24).backgroundColor('#FFFFFF') }
}
