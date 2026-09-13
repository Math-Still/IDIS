import deviceManager from '@ohos.distributedDeviceManager';

const BUNDLE_NAME: string = 'com.smartfactory.shell';
const PERMISSION_NAME: string = 'ohos.permission.DISTRIBUTED_DATASYNC';

export class NativeDistributedDevice {
  deviceId: string = '';
  networkId: string | null = null;
  deviceName: string = '';
  deviceType: string = '';
  state: string = 'UNKNOWN';
  lastSeenTs: number = 0;
}

export class NativeDiscoveryStatus {
  supported: boolean = false;
  provider: string = '@ohos.distributedDeviceManager';
  initialized: boolean = false;
  watching: boolean = false;
  permission: string = PERMISSION_NAME;
  lastError: string | null = null;
  deviceCount: number = 0;
  scope: string = 'trusted-online-devices';
}

class DeviceStateData {
  action: deviceManager.DeviceStateChange = 0;
  device: deviceManager.DeviceBasicInfo = {
    deviceId: '',
    deviceName: '',
    deviceType: '',
    networkId: ''
  };
}

export type NativeProviderEventSink = (event: string, data: Object) => void;

/**
 * OpenHarmony public application-layer binding for distributed device status.
 *
 * It intentionally uses @ohos.distributedDeviceManager rather than linking a
 * private DSoftBus C API directly. The DeviceManager system capability is the
 * stable app-facing entry point for querying trusted online devices and
 * listening for state changes on API 10 class systems.
 *
 * Active discovery/authentication of untrusted devices is intentionally not
 * implemented here because API shape, privileges and HongZOS SDK exposure must
 * be verified against the actual competition image/SDK before enabling it.
 */
export class DistributedDeviceProvider {
  private dm: deviceManager.DeviceManager | undefined = undefined;
  private watching: boolean = false;
  private lastError: string | null = null;
  private lastDeviceCount: number = 0;
  private readonly sink: NativeProviderEventSink;
  private readonly stateCallback: (data: DeviceStateData) => void;

  constructor(sink: NativeProviderEventSink) {
    this.sink = sink;
    this.stateCallback = (data: DeviceStateData): void => {
      const state: string = this.stateName(data.action);
      const device: NativeDistributedDevice = this.mapDevice(data.device, state);
      this.sink('softbus.deviceStateChanged', {
        action: state,
        device: device
      });
    };
    this.initialize();
  }

  private initialize(): void {
    try {
      this.dm = deviceManager.createDeviceManager(BUNDLE_NAME);
      this.lastError = null;
    } catch (error) {
      this.dm = undefined;
      this.lastError = `createDeviceManager failed: ${String(error)}`;
    }
  }

  private ensureInitialized(): void {
    // Permissions may be granted after an earlier initialization attempt. Retry lazily
    // instead of permanently freezing the provider in an unsupported state.
    if (this.dm === undefined) this.initialize();
  }

  isSupported(): boolean {
    this.ensureInitialized();
    return this.dm !== undefined;
  }

  status(): NativeDiscoveryStatus {
    const status: NativeDiscoveryStatus = new NativeDiscoveryStatus();
    status.supported = this.isSupported();
    status.initialized = this.dm !== undefined;
    status.watching = this.watching;
    status.lastError = this.lastError;
    status.deviceCount = this.lastDeviceCount;
    return status;
  }

  snapshot(): Array<NativeDistributedDevice> {
    this.ensureInitialized();
    if (this.dm === undefined) {
      throw new Error(this.lastError || 'Distributed DeviceManager is not initialized.');
    }

    try {
      const list: Array<deviceManager.DeviceBasicInfo> = this.dm.getAvailableDeviceListSync();
      const result: Array<NativeDistributedDevice> = [];
      for (let i: number = 0; i < list.length; i++) {
        result.push(this.mapDevice(list[i], 'AVAILABLE'));
      }
      this.lastDeviceCount = result.length;
      this.lastError = null;
      return result;
    } catch (error) {
      this.lastError = `getAvailableDeviceListSync failed: ${String(error)}`;
      throw error;
    }
  }

  watch(): NativeDiscoveryStatus {
    this.ensureInitialized();
    if (this.dm === undefined) {
      throw new Error(this.lastError || 'Distributed DeviceManager is not initialized.');
    }
    if (this.watching) return this.status();

    try {
      this.dm.on('deviceStateChange', this.stateCallback);
      this.watching = true;
      this.lastError = null;
      return this.status();
    } catch (error) {
      this.lastError = `deviceStateChange subscription failed: ${String(error)}`;
      throw error;
    }
  }

  unwatch(): NativeDiscoveryStatus {
    if (this.dm === undefined || !this.watching) return this.status();
    try {
      this.dm.off('deviceStateChange', this.stateCallback);
      this.watching = false;
      this.lastError = null;
      return this.status();
    } catch (error) {
      this.lastError = `deviceStateChange unsubscribe failed: ${String(error)}`;
      throw error;
    }
  }

  private mapDevice(info: deviceManager.DeviceBasicInfo, state: string): NativeDistributedDevice {
    const device: NativeDistributedDevice = new NativeDistributedDevice();
    device.deviceId = info.deviceId;
    device.networkId = info.networkId || null;
    device.deviceName = info.deviceName;
    device.deviceType = info.deviceType;
    device.state = state;
    device.lastSeenTs = Date.now();
    return device;
  }

  private stateName(action: deviceManager.DeviceStateChange): string {
    if (action === deviceManager.DeviceStateChange.AVAILABLE) return 'AVAILABLE';
    if (action === deviceManager.DeviceStateChange.UNAVAILABLE) return 'UNAVAILABLE';
    return 'UNKNOWN';
  }
}
