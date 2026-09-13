import deviceInfo from '@ohos.deviceInfo';
import connection from '@ohos.net.connection';
import { BridgeRequest } from './BridgeModels';
import { DistributedDeviceProvider } from '../native/DistributedDeviceProvider';

const BRIDGE_VERSION: string = '1.0.0';
const SHELL_VERSION: string = '0.4.2';
const SOURCE_NAME: string = 'openharmony-native-shell';
const MAX_BRIDGE_REQUEST_CHARS: number = 64 * 1024;

export type BridgeEventSink = (eventJson: string) => void;

export class BridgeService {
  private readonly eventSink: BridgeEventSink;
  private readonly distributedDevices: DistributedDeviceProvider;
  private eventSequence: number = 0;
  private lastNativeError: string | null = null;

  constructor(eventSink: BridgeEventSink) {
    this.eventSink = eventSink;
    this.distributedDevices = new DistributedDeviceProvider((event: string, data: Object): void => {
      this.emitNativeEvent(event, data, '@ohos.distributedDeviceManager');
    });
  }

  invoke(requestJson: string): string {
    if (typeof requestJson !== 'string' || requestJson.length === 0 || requestJson.length > MAX_BRIDGE_REQUEST_CHARS) {
      return this.failure('', 'BRIDGE_PROTOCOL_ERROR', 'Request is empty or exceeds the Bridge request size limit.', false);
    }

    let request: BridgeRequest;
    try {
      request = JSON.parse(requestJson) as BridgeRequest;
    } catch (_error) {
      return this.failure('', 'BRIDGE_PROTOCOL_ERROR', 'Request is not valid JSON.', false);
    }

    const id: string = request.id || '';
    const version: string = request.bridgeVersion || '';
    const method: string = request.method || '';
    if (id.length === 0 || id.length > 128 || method.length === 0 || method.length > 128 ||
        typeof request.timestamp !== 'number' || request.timestamp < 0) {
      return this.failure(id, 'BRIDGE_PROTOCOL_ERROR', 'Request envelope is invalid.', false);
    }

    if (version !== BRIDGE_VERSION) {
      return this.failure(id, 'VERSION_INCOMPATIBLE',
        `Bridge version mismatch. native=${BRIDGE_VERSION}, web=${version}`, false);
    }

    try {
      switch (method) {
        case 'bridge.handshake':
          return this.success(id, {
            platform: 'openharmony',
            bridgeVersion: BRIDGE_VERSION,
            shellVersion: SHELL_VERSION,
            capabilities: this.capabilities(),
            versions: {
              webVersion: 'provided-by-web',
              shellVersion: SHELL_VERSION,
              bridgeVersion: BRIDGE_VERSION,
              backendApiVersion: 'v1'
            },
            provider: {
              name: '@ohos.distributedDeviceManager',
              initialized: this.distributedDevices.isSupported(),
              lastError: this.distributedDevices.status().lastError
            }
          });
        case 'bridge.health':
          return this.success(id, {
            adapterReady: true,
            bridgeReady: true,
            networkOnline: this.networkOnline(),
            lastError: this.lastNativeError,
            nativeEventSequence: this.eventSequence,
            nativeProvider: '@ohos.distributedDeviceManager'
          });
        case 'system.getRuntimeInfo':
          return this.success(id, {
            platform: 'openharmony',
            userAgent: 'ArkWeb/OpenHarmony',
            language: 'zh-CN',
            timezone: 'system',
            online: this.networkOnline()
          });
        case 'system.getDeviceInfo':
          return this.success(id, {
            model: deviceInfo.productModel,
            architecture: deviceInfo.abiList.toString(),
            osVersion: deviceInfo.osFullName
          });
        case 'network.getStatus':
          return this.success(id, { online: this.networkOnline(), effectiveType: null });
        case 'capability.list':
          return this.success(id, this.capabilities());
        case 'app.getVersion':
          return this.success(id, { shellVersion: SHELL_VERSION, bridgeVersion: BRIDGE_VERSION });
        case 'softbus.status':
          return this.success(id, this.distributedDevices.status());
        case 'softbus.discovery.snapshot':
          return this.success(id, this.distributedDevices.snapshot());
        case 'softbus.discovery.watch':
          return this.success(id, this.distributedDevices.watch());
        case 'softbus.discovery.unwatch':
          return this.success(id, this.distributedDevices.unwatch());
        default:
          return this.failure(id, 'CAPABILITY_UNSUPPORTED',
            `Bridge method is not supported: ${method}`, true);
      }
    } catch (error) {
      this.lastNativeError = String(error);
      const message: string = String(error);
      const permissionDenied: boolean = message.indexOf('201') >= 0 || message.indexOf('permission') >= 0;
      return this.failure(id, permissionDenied ? 'PERMISSION_DENIED' : 'NATIVE_ERROR', message, true);
    }
  }

  private capabilities(): Array<string> {
    const caps: Array<string> = [
      'runtime.info',
      'device.info',
      'network.status',
      'bridge.health',
      'native.events',
      'native.invoke'
    ];
    if (this.distributedDevices.isSupported()) {
      caps.push('softbus.discovery');
      caps.push('softbus.device-state');
    }
    return caps;
  }

  private emitNativeEvent(event: string, data: Object, source: string): void {
    this.eventSequence += 1;
    const eventJson: string = JSON.stringify({
      type: 'event',
      eventId: `native-${Date.now()}-${this.eventSequence}`,
      event: event,
      data: data,
      timestamp: Date.now(),
      sequence: this.eventSequence,
      source: source || SOURCE_NAME
    });
    try {
      this.eventSink(eventJson);
    } catch (error) {
      this.lastNativeError = `Native event sink failed: ${String(error)}`;
    }
  }

  private networkOnline(): boolean {
    try {
      connection.getDefaultNetSync();
      return true;
    } catch (_error) {
      return false;
    }
  }

  private success(id: string, result: Object): string {
    return JSON.stringify({ bridgeVersion: BRIDGE_VERSION, id: id, ok: true, result: result, error: null });
  }

  private failure(id: string, code: string, message: string, recoverable: boolean): string {
    return JSON.stringify({
      bridgeVersion: BRIDGE_VERSION,
      id: id,
      ok: false,
      result: null,
      error: { code: code, message: message, recoverable: recoverable }
    });
  }
}
