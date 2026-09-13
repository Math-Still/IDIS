export type PlatformType='browser'|'uos'|'kylin'|'openharmony'|'hongzos'|'unknown';

export type Capability=
  |'runtime.info'
  |'device.info'
  |'network.status'
  |'bridge.health'
  |'native.events'
  |'native.invoke'
  |'softbus.discovery'
  |'softbus.device-state'
  |'softbus.connect'
  |'storage.native'
  |string;

export interface RuntimeInfo{
  platform:PlatformType;
  userAgent:string;
  language:string;
  timezone:string;
  online:boolean;
}

export interface DeviceInfo{
  model:string|null;
  architecture:string|null;
  osVersion:string|null;
}

export interface NetworkStatus{
  online:boolean;
  effectiveType?:string|null;
}

export interface PlatformHealth{
  adapterReady:boolean;
  bridgeReady:boolean;
  networkOnline:boolean;
  lastError:string|null;
  nativeEventSequence?:number;
  nativeProvider?:string|null;
}

export type DistributedDeviceState='UNKNOWN'|'AVAILABLE'|'UNAVAILABLE';

export interface DistributedDevice{
  deviceId:string;
  networkId:string|null;
  deviceName:string;
  deviceType:string;
  state:DistributedDeviceState;
  lastSeenTs:number;
}

export interface NativeDiscoveryStatus{
  supported:boolean;
  provider:string;
  initialized:boolean;
  watching:boolean;
  permission:string;
  lastError:string|null;
  deviceCount:number;
}

export interface PlatformAdapter{
  getPlatform():Promise<PlatformType>;
  getRuntimeInfo():Promise<RuntimeInfo>;
  getDeviceInfo():Promise<DeviceInfo>;
  getNetworkStatus():Promise<NetworkStatus>;
  getCapabilities():Promise<Capability[]>;
  hasCapability(c:Capability):Promise<boolean>;
  health():Promise<PlatformHealth>;
  invokeNativeCapability<TResult=unknown,TParams=unknown>(method:string,params?:TParams):Promise<TResult>;
  onNativeEvent(handler:(event:unknown)=>void):()=>void;
}
