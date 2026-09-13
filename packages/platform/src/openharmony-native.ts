import type {
  Capability,DeviceInfo,NetworkStatus,PlatformAdapter,PlatformHealth,PlatformType,RuntimeInfo
} from './types';
import { NativeBridgeClient } from './native-bridge';

interface NativeRuntimeInfo extends RuntimeInfo { platform:PlatformType; }

export class OpenHarmonyNativeAdapter implements PlatformAdapter {
  constructor(private readonly bridge=new NativeBridgeClient()){}
  async getPlatform():Promise<PlatformType>{const info=await this.bridge.call<NativeRuntimeInfo>('system.getRuntimeInfo',{});return info.platform;}
  async getRuntimeInfo():Promise<RuntimeInfo>{return this.bridge.call<RuntimeInfo>('system.getRuntimeInfo',{});}
  async getDeviceInfo():Promise<DeviceInfo>{return this.bridge.call<DeviceInfo>('system.getDeviceInfo',{});}
  async getNetworkStatus():Promise<NetworkStatus>{return this.bridge.call<NetworkStatus>('network.getStatus',{});}
  async getCapabilities():Promise<Capability[]>{return this.bridge.call<Capability[]>('capability.list',{});}
  async hasCapability(c:Capability):Promise<boolean>{return(await this.getCapabilities()).includes(c);}
  async health():Promise<PlatformHealth>{return this.bridge.call<PlatformHealth>('bridge.health',{});}
  async invokeNativeCapability<TResult=unknown,TParams=unknown>(method:string,params?:TParams):Promise<TResult>{return this.bridge.call<TResult,TParams|Record<string,never>>(method,(params??{}) as TParams|Record<string,never>);}
  onNativeEvent(handler:(event:unknown)=>void):()=>void{return this.bridge.onEvent(handler);}
}
