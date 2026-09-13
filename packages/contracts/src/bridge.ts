import type { PlatformErrorPayload } from '@smart-factory/shared';
import type { VersionInfo } from './version';

export type BridgeMethod =
  | 'bridge.handshake'
  | 'bridge.health'
  | 'system.getRuntimeInfo'
  | 'system.getDeviceInfo'
  | 'network.getStatus'
  | 'capability.list'
  | 'app.getVersion'
  | 'softbus.status'
  | 'softbus.discovery.snapshot'
  | 'softbus.discovery.watch'
  | 'softbus.discovery.unwatch';

export interface BridgeRequest<T=unknown>{
  bridgeVersion:string;
  id:string;
  method:BridgeMethod|string;
  params:T;
  timestamp:number;
}

export interface BridgeSuccess<T=unknown>{
  bridgeVersion:string;
  id:string;
  ok:true;
  result:T;
  error:null;
}

export interface BridgeFailure{
  bridgeVersion:string;
  id:string;
  ok:false;
  result:null;
  error:PlatformErrorPayload;
}

export type BridgeResponse<T=unknown>=BridgeSuccess<T>|BridgeFailure;

export interface BridgeHandshake{
  platform:string;
  bridgeVersion:string;
  shellVersion:string|null;
  capabilities:string[];
  versions:VersionInfo;
  provider?:{
    name:string;
    initialized:boolean;
    lastError:string|null;
  };
}

export interface NativeEvent<T=unknown>{
  type:'event';
  eventId:string;
  event:string;
  data:T;
  timestamp:number;
  sequence:number;
  source:string;
}
