import { worker } from '@kit.ArkTS';
import lifetime from 'libnativeapi_clipboard_lifetime.so';

worker.workerPort.onmessage = (event): void => {
  const pending = event.data === 'pending';
  lifetime.begin(pending);
  if (pending) {
    worker.workerPort.postMessage('queued');
    // Close the actual environment in this JS turn, before returning to its
    // event loop and delivering any of the pending native reads.
    worker.workerPort.close();
  } else {
    setTimeout(() => { worker.workerPort.postMessage('completed'); }, 200);
  }
};
