import web_webview from '@ohos.web.webview';
import { BridgeService } from '../bridge/BridgeService';
import { WebEventDispatcher } from '../bridge/WebEventDispatcher';

class WebBridgeProxy {
  private readonly service: BridgeService;

  constructor(service: BridgeService) {
    this.service = service;
  }

  invoke(requestJson: string): string {
    return this.service.invoke(requestJson);
  }
}

@Entry
@Component
struct Index {
  private controller: web_webview.WebviewController = new web_webview.WebviewController();
  private eventDispatcher: WebEventDispatcher = new WebEventDispatcher(this.controller);
  private bridgeService: BridgeService = new BridgeService((eventJson: string): void => {
    this.eventDispatcher.emit(eventJson);
  });
  private bridgeProxy: WebBridgeProxy = new WebBridgeProxy(this.bridgeService);

  build() {
    Column() {
      Web({ src: $rawfile('web/index.html'), controller: this.controller })
        .width('100%')
        .height('100%')
        .javaScriptAccess(true)
        .fileAccess(false)
        .javaScriptProxy({
          object: this.bridgeProxy,
          name: 'smartFactoryNative',
          methodList: ['invoke'],
          controller: this.controller
        })
        .onPageBegin(() => {
          this.eventDispatcher.setReady(false);
        })
        .onPageEnd(() => {
          this.eventDispatcher.setReady(true);
          console.info('SmartFactoryShell: Web Core page loaded and native event channel ready.');
        })
    }
    .width('100%')
    .height('100%')
  }
}
