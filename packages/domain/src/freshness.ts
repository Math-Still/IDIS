import type { CommunicationHealth, DashboardSnapshot, DataQuality, TelemetryPoint } from './models';
export interface FreshnessPolicy { staleAfterMs:number; badAfterMs:number; }
export const DEFAULT_FRESHNESS_POLICY:FreshnessPolicy={staleAfterMs:10_000,badAfterMs:60_000};
export function deriveFreshnessQuality(point:Pick<TelemetryPoint,'quality'|'receiveTs'>,now:number,policy:FreshnessPolicy=DEFAULT_FRESHNESS_POLICY):DataQuality{
  if(point.quality==='BAD') return 'BAD'; const age=Math.max(0,now-point.receiveTs); if(age>=policy.badAfterMs)return'BAD'; if(age>=policy.staleAfterMs)return'STALE'; return point.quality;
}
export function deriveCommunicationQuality(item:Pick<CommunicationHealth,'quality'|'lastSeen'|'online'>,now:number,policy:FreshnessPolicy=DEFAULT_FRESHNESS_POLICY):DataQuality{
  if(!item.online||item.quality==='BAD')return'BAD'; if(item.lastSeen===undefined)return item.quality==='GOOD'?'UNCERTAIN':item.quality; const age=Math.max(0,now-item.lastSeen); if(age>=policy.badAfterMs)return'BAD'; if(age>=policy.staleAfterMs)return'STALE'; return item.quality;
}
export function applyFreshness(snapshot:DashboardSnapshot,now:number,policy:FreshnessPolicy=DEFAULT_FRESHNESS_POLICY):DashboardSnapshot{
  const telemetry=snapshot.telemetry.map(point=>({...point,quality:deriveFreshnessQuality(point,now,policy)}));
  const communication=snapshot.communication.map(item=>({...item,quality:deriveCommunicationQuality(item,now,policy),online:item.lastSeen===undefined?item.online:(now-item.lastSeen<policy.badAfterMs&&item.online)}));
  const healthByDevice=new Map(communication.map(item=>[item.deviceId,item]));
  const devices=snapshot.devices.map(device=>{const health=healthByDevice.get(device.id); if(!health||!health.online)return{...device,status:'OFFLINE' as const}; if(health.quality!=='GOOD')return{...device,status:'DEGRADED' as const}; if(device.status==='MAINTENANCE')return device; return{...device,status:'ONLINE' as const};});
  return{...snapshot,generatedAt:now,telemetry,communication,devices};
}
