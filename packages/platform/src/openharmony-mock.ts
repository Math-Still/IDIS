import { unsupportedCapability } from '@smart-factory/shared';
import type { Capability,DeviceInfo,NetworkStatus,PlatformAdapter,PlatformHealth,PlatformType,RuntimeInfo } from './types';

export class OpenHarmonyMockAdapter implements PlatformAdapter {
  private readonly capabilities:Capability[]=['runtime.info','device.info','network.status','bridge.health','native.events','native.invoke'];
  async getPlatform():Promise<PlatformType>{return'openharmony';}
  async getRuntimeInfo():Promise<RuntimeInfo>{return{platform:'openharmony',userAgent:'OpenHarmony-Mock/Phase-1',language:'zh-CN',timezone:'Asia/Shanghai',online:true};}
  async getDeviceInfo():Promise<DeviceInfo>{return{model:'mock-device',architecture:'arm64',osVersion:'mock'};}
  async getNetworkStatus():Promise<NetworkStatus>{return{online:true,effectiveType:null};}
  async getCapabilities():Promise<Capability[]>{return[...this.capabilities];}
  async hasCapability(c:Capability):Promise<boolean>{return this.capabilities.includes(c);}
  async health():Promise<PlatformHealth>{return{adapterReady:true,bridgeReady:false,networkOnline:true,lastError:'Native bridge is not implemented in mock mode.',nativeProvider:'mock'};}
  async invokeNativeCapability<TResult=unknown,TParams=unknown>(method:string,_params?:TParams):Promise<TResult>{throw unsupportedCapability(method);}
  onNativeEvent(_handler:(event:unknown)=>void):()=>void{return()=>undefined;}
}
