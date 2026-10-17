import { UIAbility, Want } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import { pasteboard } from '@kit.BasicServicesKit';
import { fileIo, fileUri } from '@kit.CoreFileKit';
import { image } from '@kit.ImageKit';
import { util } from '@kit.ArkTS';
import { inputConsumer, KeyCode } from '@kit.InputKit';

const pixels: number[] = [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255,
  255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 0, 255];
interface Snapshot { text: string; html: string; uris: string[]; contents: string[]; pixels: number[]; pixelFormat: number; }
export default class EntryAbility extends UIAbility {
  private hotkeys: inputConsumer.HotkeyOptions[] = [];
  private activations: number = 0;
  private hotkeyCallback: (options: inputConsumer.HotkeyOptions) => void = (options) => { ++this.activations; };
  private hotkeyCommand(command: string, request: string): void {
    let code = 0;
    try {
      if (command === 'hotkey-acquire') {
        for (const ctrl of [KeyCode.KEYCODE_CTRL_LEFT, KeyCode.KEYCODE_CTRL_RIGHT]) {
          for (const alt of [KeyCode.KEYCODE_ALT_LEFT, KeyCode.KEYCODE_ALT_RIGHT]) {
            const options: inputConsumer.HotkeyOptions = { preKeys: [ctrl, alt], finalKey: KeyCode.KEYCODE_F9, isRepeat: false };
            inputConsumer.on('hotkeyChange', options, this.hotkeyCallback);
            this.hotkeys.push(options);
          }
        }
      }
      if (command === 'hotkey-release') {
        for (const options of this.hotkeys) { inputConsumer.off('hotkeyChange', options, this.hotkeyCallback); }
        this.hotkeys = [];
      }
    } catch (error) { code = error.code; }
    const output = JSON.stringify({ code: code, held: this.hotkeys.length, callbacks: this.activations });
    AppStorage.setOrCreate('peerStatus', output);
    console.info(`NATIVEAPI_SHORTCUT_PEER_REPLY ${request} ${command} ${output}`);
  }
  onDestroy(): void { this.hotkeyCommand('hotkey-release', '0'); }
  onCreate(want: Want): void { AppStorage.setOrCreate('peerStatus', 'Ready'); }
  onWindowStageCreate(stage: window.WindowStage): void { stage.loadContent('pages/Index'); }
  onNewWant(want: Want): void {
    const command = want.parameters?.['command'];
    const request = want.parameters?.['request'];
    if (typeof command === 'string' && typeof request === 'string' &&
        ['hotkey-acquire', 'hotkey-release', 'hotkey-state'].includes(command)) {
      this.hotkeyCommand(command, request); return;
    }
    if (typeof command !== 'string' || typeof request !== 'string' || !/^\d+$/.test(request) ||
        !['publish', 'publish-directory', 'delete-files', 'read', 'clear'].includes(command)) { return; }
    this.execute(command, request);
  }
  private async execute(command: string, request: string): Promise<void> {
    try {
      const board = pasteboard.getSystemPasteboard();
      if (command === 'publish') {
        const paths = [this.context.filesDir + '/cross-app-中文 space+#%.txt',
          this.context.filesDir + '/second.txt'];
        for (let i = 0; i < paths.length; ++i) {
          const f = fileIo.openSync(paths[i], fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.TRUNC);
          try { fileIo.writeSync(f.fd, `peer-payload-${i}-中文🙂`); } finally { fileIo.closeSync(f); }
        }
        const map = await image.createPixelMap(new Uint8Array(pixels).buffer,
          { size: { width: 3, height: 2 }, srcPixelFormat: image.PixelMapFormat.RGBA_8888, pixelFormat: image.PixelMapFormat.RGBA_8888 });
        const data = pasteboard.createData(pasteboard.MIMETYPE_TEXT_PLAIN, 'peer-text-中文🙂');
        data.addRecord(pasteboard.MIMETYPE_TEXT_HTML, '<b>peer-html-中文🙂</b>');
        data.addRecord(pasteboard.MIMETYPE_PIXELMAP, map);
        for (let i = paths.length - 1; i >= 0; --i) {
          data.addRecord(pasteboard.MIMETYPE_TEXT_URI, fileUri.getUriFromPath(paths[i]));
        }
        const property = data.getProperty();
        property.shareOption = pasteboard.ShareOption.LOCALDEVICE;
        data.setProperty(property);
        await board.setData(data);
        map.release();
      }
      if (command === 'publish-directory') {
        const root = this.context.filesDir + '/folder-中文 +#%';
        if (!fileIo.accessSync(root + '/nested')) { fileIo.mkdirSync(root + '/nested', true); }
        const names = ['/payload.txt', '/nested/child.txt'];
        const values = ['peer-folder-中文🙂', 'peer-nested-中文🙂'];
        for (let i = 0; i < names.length; ++i) {
          const file = fileIo.openSync(root + names[i], fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.TRUNC);
          try { fileIo.writeSync(file.fd, values[i]); } finally { fileIo.closeSync(file); }
        }
        const data = pasteboard.createData(pasteboard.MIMETYPE_TEXT_URI, fileUri.getUriFromPath(root));
        const property = data.getProperty(); property.shareOption = pasteboard.ShareOption.LOCALDEVICE; data.setProperty(property);
        await board.setData(data);
      }
      if (command === 'delete-files') {
        fileIo.unlinkSync(this.context.filesDir + '/cross-app-中文 space+#%.txt');
        fileIo.unlinkSync(this.context.filesDir + '/second.txt');
        console.info(`NATIVEAPI_CLIPBOARD_PEER_REPLY ${request} ${command} {"deleted":2}`);
        return;
      }
      if (command === 'clear') { await board.clearData(); }
      const data = await board.getData();
      const result: Snapshot = { text: data.getPrimaryText() ?? '', html: data.getPrimaryHtml() ?? '',
        uris: [], contents: [], pixels: [], pixelFormat: 0 };
      for (let i = 0; i < data.getRecordCount(); ++i) {
        const record = data.getRecord(i);
        if (record.uri) {
          result.uris.push(record.uri);
          if (fileIo.statSync(record.uri).isDirectory()) {
            const path = new fileUri.FileUri(record.uri).path;
            const names = ['/payload.txt', '/nested/child.txt'];
            const values: string[] = [];
            for (const name of names) {
              const child = fileIo.openSync(path + name, fileIo.OpenMode.READ_ONLY);
              try {
                const buffer = new ArrayBuffer(1024); const count = fileIo.readSync(child.fd, buffer);
                values.push(new util.TextDecoder('utf-8').decodeWithStream(new Uint8Array(buffer, 0, count)));
              } finally { fileIo.closeSync(child); }
            }
            result.contents.push('folder:' + values.join('|'));
            continue;
          }
          const file = fileIo.openSync(record.uri, fileIo.OpenMode.READ_ONLY);
          try {
            const buffer = new ArrayBuffer(1024);
            const count = fileIo.readSync(file.fd, buffer);
            result.contents.push(new util.TextDecoder('utf-8').decodeWithStream(new Uint8Array(buffer, 0, count)));
          } finally { fileIo.closeSync(file); }
        }
        if (record.pixelMap) {
          const buffer = new ArrayBuffer(24);
          await record.pixelMap.readPixelsToBuffer(buffer);
          const format = record.pixelMap.getImageInfoSync().pixelFormat;
          result.pixelFormat = format;
          result.pixels = Array.from(new Uint8Array(buffer));
          if (format === image.PixelMapFormat.BGRA_8888) {
            for (let i = 0; i < result.pixels.length; i += 4) {
              const red = result.pixels[i + 2];
              result.pixels[i + 2] = result.pixels[i];
              result.pixels[i] = red;
            }
          } else if (format !== image.PixelMapFormat.RGBA_8888) {
            throw new Error('Unexpected SDK pixel format: ' + format);
          }
        }
      }
      const output = JSON.stringify(result);
      AppStorage.setOrCreate('peerStatus', output);
      console.info(`NATIVEAPI_CLIPBOARD_PEER_REPLY ${request} ${command} ${output}`);
    } catch (error) {
      const output = JSON.stringify(error);
      AppStorage.setOrCreate('peerStatus', 'Failed: ' + output);
      console.error(`NATIVEAPI_CLIPBOARD_PEER_ERROR ${request} ${command} ${output}`);
    }
  }
}
