import type { RuntimeConfig } from '@smart-factory/contracts';
import type {
  ApplicationInstance,
  ConfigDraftResponse,
  ConfigPublishResponse,
  ConfigValidationResponse,
  PlatformAsset,
  PlatformConfigSnapshot,
  PlatformConfigStatus,
  LatestTelemetryResponse,
  PlatformTelemetryHistoryResponse,
  PlatformRule, AlarmOccurrenceV2, IncidentV2, TimelineResponseV2, Command, CommandPreviewV2, CommandSubmitV2, CommandReconciliationInput, ReplaySessionV2, ReportJobV2, AgentStatusV2, AgentReplyV2
} from '@smart-factory/domain';
import { ApiError } from './rest';

export class PlatformApiClient {
  private accessToken: string | null = null;
  constructor(private readonly config: RuntimeConfig) {}
  setAccessToken(token:string|null):void { this.accessToken=token?.trim()||null; }
  private url(path:string):string { return `${this.config.apiBaseUrl.replace(/\/$/,'')}/v2/${path.replace(/^\//,'')}`; }
  private async request<T>(path:string, init:RequestInit={}):Promise<T> {
    const controller=new AbortController();
    const timeoutMs=this.config.transport?.httpRequestTimeoutMs??8000;
    const timer=setTimeout(()=>controller.abort(),timeoutMs);
    try {
      const response=await fetch(this.url(path),{...init,signal:controller.signal,headers:{Accept:'application/json',...(init.body?{'Content-Type':'application/json'}:{}),...(this.accessToken?{Authorization:`Bearer ${this.accessToken}`}:{}) ,...(init.headers??{})}});
      const text=await response.text();
      let payload:unknown=null; try{payload=text?JSON.parse(text):null;}catch{payload=text;}
      if(!response.ok){const body=payload&&typeof payload==='object'?payload as Record<string,unknown>:{};throw new ApiError(typeof body.error==='string'?body.error:`${init.method??'GET'} ${path} failed: ${response.status}`,{status:response.status,code:typeof body.code==='string'?body.code:null,details:payload});}
      return payload as T;
    } catch(error) {
      if(error instanceof Error&&error.name==='AbortError') throw new ApiError(`${init.method??'GET'} ${path} timed out after ${timeoutMs} ms`);
      throw error;
    } finally { clearTimeout(timer); }
  }
  getConfigStatus():Promise<PlatformConfigStatus>{return this.request('config/status');}
  getConfigSnapshot():Promise<PlatformConfigSnapshot>{return this.request('config/snapshot');}
  getAssets():Promise<PlatformAsset[]>{return this.request('assets');}
  getApplicationInstances():Promise<ApplicationInstance[]>{return this.request('application-instances');}
  getRules():Promise<PlatformRule[]>{return this.request('rules');}
  getAgentStatus():Promise<AgentStatusV2>{return this.request('agent/status');}
  chatWithAgent(message:string,history:Array<{role:'user'|'assistant';content:string}>=[]):Promise<AgentReplyV2>{return this.request('agent/chat',{method:'POST',body:JSON.stringify({message,history})});}
  getAlarmOccurrences(query:{deviceId?:string;sourceDomain?:string}={}):Promise<AlarmOccurrenceV2[]>{const p=new URLSearchParams();if(query.deviceId)p.set('deviceId',query.deviceId);if(query.sourceDomain)p.set('sourceDomain',query.sourceDomain);const q=p.toString();return this.request(`alarm-occurrences${q?`?${q}`:''}`);}
  acknowledgeAlarmOccurrence(id:string,comment?:string):Promise<AlarmOccurrenceV2>{return this.request(`alarm-occurrences/${encodeURIComponent(id)}/acknowledge`,{method:'POST',body:JSON.stringify({comment})});}
  getIncidents():Promise<IncidentV2[]>{return this.request('incidents');}
  createIncident(input:{occurrenceIds:string[];title?:string;priority?:string;assignee?:string;description?:string}):Promise<IncidentV2>{return this.request('incidents',{method:'POST',body:JSON.stringify(input)});}
  transitionIncident(id:string,input:{action:'START'|'RESOLVE'|'CLOSE'|'REOPEN'|'ASSIGN';expectedRevision:number;comment?:string;assignee?:string}):Promise<IncidentV2>{return this.request(`incidents/${encodeURIComponent(id)}/transitions`,{method:'POST',body:JSON.stringify(input)});}
  getTimeline(query:{from?:number;to?:number;instanceId?:string}={}):Promise<TimelineResponseV2>{const p=new URLSearchParams();if(query.from!==undefined)p.set('from',String(query.from));if(query.to!==undefined)p.set('to',String(query.to));if(query.instanceId)p.set('instanceId',query.instanceId);const q=p.toString();return this.request(`timeline${q?`?${q}`:''}`);}
  createReplaySession(input:{instanceId:string;from:number;to:number;limit?:number;configRevision?:string}):Promise<ReplaySessionV2>{return this.request('replay-sessions',{method:'POST',body:JSON.stringify(input)});}
  getReplaySession(id:string):Promise<ReplaySessionV2>{return this.request(`replay-sessions/${encodeURIComponent(id)}`);}
  createReportJob(input:{instanceId:string;from:number;to:number;format?:string}):Promise<ReportJobV2>{return this.request('report-jobs',{method:'POST',body:JSON.stringify(input)});}
  getReportJob(id:string):Promise<ReportJobV2>{return this.request(`report-jobs/${encodeURIComponent(id)}`);}
  getLatestTelemetry(query:{deviceId?:string;pointId?:string}={}):Promise<LatestTelemetryResponse>{const p=new URLSearchParams();if(query.deviceId)p.set('deviceId',query.deviceId);if(query.pointId)p.set('pointId',query.pointId);const q=p.toString();return this.request(`telemetry/latest${q?`?${q}`:''}`);}

  previewCommand(input:{deviceId:string;action:string;requestedValue?:unknown;reason:string}):Promise<CommandPreviewV2>{return this.request('commands/preview',{method:'POST',body:JSON.stringify(input)});}
  issueCommand(input:CommandSubmitV2):Promise<Command>{return this.request('commands',{method:'POST',body:JSON.stringify(input)});}
  getCommand(id:string):Promise<Command>{return this.request(`commands/${encodeURIComponent(id)}`);}
  reconcileCommand(id:string,input:CommandReconciliationInput):Promise<Command>{return this.request(`commands/${encodeURIComponent(id)}/reconciliations`,{method:'POST',body:JSON.stringify(input)});}
  getTelemetryHistory(query:{deviceId?:string;pointId?:string;from?:number;to?:number;limit?:number}={}):Promise<PlatformTelemetryHistoryResponse>{const p=new URLSearchParams();if(query.deviceId)p.set('deviceId',query.deviceId);if(query.pointId)p.set('pointId',query.pointId);if(query.from!==undefined)p.set('from',String(query.from));if(query.to!==undefined)p.set('to',String(query.to));if(query.limit!==undefined)p.set('limit',String(query.limit));const q=p.toString();return this.request(`telemetry/history${q?`?${q}`:''}`);}
  createDraft(snapshot?:PlatformConfigSnapshot,baseRevision?:string):Promise<ConfigDraftResponse>{return this.request('config-drafts',{method:'POST',body:JSON.stringify({baseRevision,snapshot})});}
  validateDraft(id:string):Promise<ConfigValidationResponse>{return this.request(`config-drafts/${encodeURIComponent(id)}/validate`,{method:'POST',body:'{}'});}
  publishDraft(id:string,reason:string):Promise<ConfigPublishResponse>{return this.request(`config-drafts/${encodeURIComponent(id)}/publish`,{method:'POST',body:JSON.stringify({reason})});}
  rollback(revision:string,reason:string):Promise<ConfigPublishResponse>{return this.request(`config-revisions/${encodeURIComponent(revision)}/rollback`,{method:'POST',body:JSON.stringify({reason})});}
}
