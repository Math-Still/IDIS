import web_webview from '@ohos.web.webview';

/**
 * Native -> Web event channel.
 * Events are queued until ArkWeb reports page load completion, then delivered
 * through the single frozen JS callback __SMART_FACTORY_NATIVE_EVENT__.
 */
export class WebEventDispatcher {
  private readonly controller: web_webview.WebviewController;
  private ready: boolean = false;
  private queue: Array<string> = [];
  private readonly maxQueue: number = 64;
  private readonly maxEventChars: number = 128 * 1024;
  private lastError: string | null = null;

  constructor(controller: web_webview.WebviewController) {
    this.controller = controller;
  }

  setReady(ready: boolean): void {
    this.ready = ready;
    if (ready) this.flush();
  }

  emit(eventJson: string): void {
    if (eventJson.length > this.maxEventChars) {
      this.lastError = 'Native event exceeds size limit.';
      return;
    }
    if (!this.ready) {
      if (this.queue.length >= this.maxQueue) this.queue.shift();
      this.queue.push(eventJson);
      return;
    }
    this.deliver(eventJson);
  }

  getLastError(): string | null {
    return this.lastError;
  }

  private flush(): void {
    while (this.ready && this.queue.length > 0) {
      const eventJson: string | undefined = this.queue.shift();
      if (eventJson !== undefined) this.deliver(eventJson);
    }
  }

  private deliver(eventJson: string): void {
    try {
      const arg: string = JSON.stringify(eventJson);
      const script: string = `window.__SMART_FACTORY_NATIVE_EVENT__ && window.__SMART_FACTORY_NATIVE_EVENT__(${arg});`;
      this.controller.runJavaScript(script);
      this.lastError = null;
    } catch (error) {
      this.lastError = `runJavaScript event delivery failed: ${String(error)}`;
      console.error(`SmartFactoryShell: ${this.lastError}`);
    }
  }
}
