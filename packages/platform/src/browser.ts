import { unsupportedCapability } from '@smart-factory/shared';
import type {
  Capability,DeviceInfo,NetworkStatus,PlatformAdapter,PlatformHealth,PlatformType,RuntimeInfo
} from './types';

export class BrowserPlatformAdapter implements PlatformAdapter {
  async getPlatform():Promise<PlatformType>{return'browser';}
  async getRuntimeInfo():Promise<RuntimeInfo>{return{platform:'browser',userAgent:navigator.userAgent,language:navigator.language,timezone:Intl.DateTimeFormat().resolvedOptions().timeZone,online:navigator.onLine};}
  async getDeviceInfo():Promise<DeviceInfo>{return{model:null,architecture:null,osVersion:null};}
  async getNetworkStatus():Promise<NetworkStatus>{const nav=navigator as Navigator&{connection?:{effectiveType?:string}};return{online:navigator.onLine,effectiveType:nav.connection?.effectiveType??null};}
  async getCapabilities():Promise<Capability[]>{return['runtime.info','device.info','network.status','bridge.health'];}
  async hasCapability(c:Capability):Promise<boolean>{return(await this.getCapabilities()).includes(c);}
  async health():Promise<PlatformHealth>{return{adapterReady:true,bridgeReady:false,networkOnline:navigator.onLine,lastError:null,nativeProvider:null};}
  async invokeNativeCapability<TResult=unknown,TParams=unknown>(method:string,_params?:TParams):Promise<TResult>{throw unsupportedCapability(method);}
  onNativeEvent(handler:(event:unknown)=>void):()=>void{const online=()=>handler({type:'network.changed',online:true});const offline=()=>handler({type:'network.changed',online:false});window.addEventListener('online',online);window.addEventListener('offline',offline);return()=>{window.removeEventListener('online',online);window.removeEventListener('offline',offline);};}
}
