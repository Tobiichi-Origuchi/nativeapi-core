import nativeTests from 'libnativeapi_smoke.so';
import { picker } from '@kit.CoreFileKit';
import { common } from '@kit.AbilityKit';

@Entry
@Component
struct Index {
  @State result: string = 'Running NativeAPI tests...';
  @State summary: string = 'Running...';
  @State ready: boolean = false;
  @State baseSummary: string = '';
  aboutToAppear(): void {
    if (AppStorage.get<boolean>('nativeapiNotificationOnly')) {
      this.summary = 'Running native notification tests...';
      setTimeout(() => {
        nativeTests.runNotifications(AppStorage.get<string>('nativeapiNotificationMode') ?? 'normal').then((report: string) => {
          this.result = report;
          this.summary = report.substring(report.lastIndexOf('NATIVEAPI_NOTIFICATION_')).trim();
        });
      }, 1500);
      return;
    }
    if (AppStorage.get<boolean>('nativeapiUrlOnly')) {
      this.summary = 'Running native URL dispatch tests...';
      setTimeout(() => {
        nativeTests.runUrls(AppStorage.get<string>('nativeapiUrl') ?? '',
          AppStorage.get<boolean>('nativeapiUrlUseC') === true).then((report: string) => {
          this.result = report;
          this.summary = report.substring(report.lastIndexOf('NATIVEAPI_URL_')).trim();
        });
      }, 1500);
      return;
    }
    if (AppStorage.get<boolean>('nativeapiPickerReference')) {
      this.summary = 'SDK picker reference pending';
      setTimeout(() => {
        const options = new picker.DocumentSelectOptions();
        options.maxSelectNumber = 10;
        console.info(`SDK_FOLDER_CAPABILITY ${canIUse('SystemCapability.FileManagement.UserFileService.FolderSelection')}`);
        if (AppStorage.get<boolean>('nativeapiPickerReferenceFolder')) {
          options.selectMode = picker.DocumentSelectMode.FOLDER;
          options.maxSelectNumber = 1;
        } else if (!AppStorage.get<boolean>('nativeapiPickerNoFilter')) {
          options.fileSuffixFilters = ['.txt,.md'];
        }
        const startUri = AppStorage.get<string>('nativeapiPickerStartUri') ?? '';
        if (startUri.length > 0) { options.defaultFilePathUri = startUri; }
        if (AppStorage.get<boolean>('nativeapiPickerAggregate')) { options.mergeMode = picker.MergeTypeMode.DOCUMENT; }
        console.info(`SDK_REFERENCE_BEGIN reference ${JSON.stringify(options)}`);
        const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
        new picker.DocumentViewPicker(context).select(options).then((uris: string[]) => {
          this.summary = `SDK_REFERENCE_COUNT ${uris.length}`;
          this.result = JSON.stringify(uris);
          console.info(`SDK_REFERENCE_RESULT ${JSON.stringify(uris)}`);
        }).catch((error: Error) => { this.summary = 'SDK_REFERENCE_FAILED'; console.error(`SDK_REFERENCE_FAILED ${JSON.stringify(error)}`); });
      }, 1000);
      return;
    }
    if (AppStorage.get<boolean>('nativeapiPickerOnly')) {
      this.ready = true;
      this.summary = 'NativeAPI system picker ready';
      this.result = 'Ready for guarded native picker interaction';
      return;
    }
    setTimeout(() => {
      nativeTests.run().then((report: string) => {
        this.result = report;
        this.summary = report.substring(report.lastIndexOf('NATIVEAPI_OHOS_')).trim();
        this.baseSummary = this.summary;
        this.ready = report.includes('NATIVEAPI_OHOS_PASS');
      });
    }, 1500);
  }
  build() {
    Scroll() {
      Column({ space: 16 }) {
        Text('NativeAPI · HarmonyOS').fontSize(24).fontWeight(FontWeight.Bold)
        Text(this.summary).fontSize(20).fontWeight(FontWeight.Bold)
        if (this.baseSummary.length > 0 && this.baseSummary !== this.summary) {
          Text(this.baseSummary).fontSize(16)
        }
        Button('Run native file picker tests').enabled(this.ready).onClick(() => {
          this.ready = false;
          this.summary = 'Running system file picker tests...';
          nativeTests.runPickers(AppStorage.get<boolean>('nativeapiPickerCancelOnly') === true).then((report: string) => {
            this.result = this.result + '\n' + report;
            this.summary = report.substring(report.lastIndexOf('NATIVEAPI_PICKER_')).trim();
          });
        })
        Text(this.result).fontSize(14).width('100%')
      }.padding(24).width('100%')
    }.width('100%').height('100%')
  }
}
