import type { ScenarioType, TelemetryPoint } from './models';

export interface PointRef { deviceId:string; pointId:string; }
export type AssetType = 'SITE'|'AREA'|'DEVICE';
export interface PlatformAsset { id:string; type:AssetType; name:string; parentId?:string; deviceId?:string; }
export interface ApplicationInstance {
  instanceId:string;
  templateId:ScenarioType|string;
  templateVersion:string;
  assetId:string;
  displayName:string;
  bindings:Record<string,PointRef>;
  ruleIds:string[];
  allowedActions:Array<{deviceId:string;actionId:string}>;
}
export interface PlatformConfigStatus {
  enabled:boolean;
  schemaVersion?:string;
  publishedRevision?:string;
  runningRevision?:string;
  state?:'RUNNING'|'PUBLISHED_REQUIRES_RESTART'|string;
  databasePath?:string;
}
export interface PlatformConfigSnapshot {
  enabled?:boolean;
  schemaVersion:string;
  site:{id:string;name:string};
  assets:PlatformAsset[];
  devices:unknown[];
  rules?:PlatformRule[];
  applicationInstances:ApplicationInstance[];
}
export interface ConfigDraftResponse { draftId:string;baseRevision:string;snapshot:PlatformConfigSnapshot; }
export interface ConfigValidationIssue { code:string;message:string;objectId?:string; }
export interface ConfigValidationResponse {
  draftId:string;baseRevision:string;valid:boolean;errors:ConfigValidationIssue[];warnings:ConfigValidationIssue[];affectedObjects:string[];requiresRestart:boolean;
}
export interface ConfigPublishResponse {
  ok:boolean;revision?:string;parentRevision?:string;rollbackTarget?:string;requiresRestart?:boolean;runningRevision?:string;state?:string;code?:string;errors?:ConfigValidationIssue[];
}

export interface LatestTelemetryResponse {
  items:TelemetryPoint[];
  generatedAt:number;
  contextId:string;
  configRevision:string;
}
export interface PlatformTelemetryHistoryResponse {
  enabled:boolean;
  items:TelemetryPoint[];
  totalMatched:number;
  limit:number;
  from:number;
  to:number;
  truncated:boolean;
  contextId?:string;
  configRevision?:string;
}


export type RuleConditionState = 'ACTIVE'|'NORMAL'|'UNKNOWN';
export type AlarmAckState = 'UNACKNOWLEDGED'|'ACKNOWLEDGED';
export interface PlatformRule {
  ruleId:string; version:string; enabled:boolean; point:PointRef; direction:'HIGH'|'LOW';
  triggerThreshold:number; recoveryThreshold:number; durationMs:number; minimumQuality:string; severity:'INFO'|'WARNING'|'CRITICAL'; title:string; message:string;
}
export interface AlarmOccurrenceV2 {
  occurrenceId:string; alarmId:string; deviceId:string; pointId?:string; ruleId?:string; ruleVersion?:string; sourceDomain:string; severity:'INFO'|'WARNING'|'CRITICAL';
  conditionState:RuleConditionState; ackState:AlarmAckState; raisedAt:number; normalizedAt:number; acknowledgedAt:number; acknowledgedBy:string; revision:number; title:string; message:string; acknowledgementComment?:string;
}
export type IncidentStatus = 'OPEN'|'IN_PROGRESS'|'RESOLVED'|'CLOSED';
export interface IncidentV2 {
  incidentId:string; status:IncidentStatus; revision:number; title:string; priority:string; assignee:string; occurrenceIds:string[]; createdAt:number; updatedAt:number; createdBy:string; description?:string; lastActor?:string; lastComment?:string;
}
export interface TimelineItemV2 { eventId:string; timestamp:number; timelineDomain:'ALARM'|'INCIDENT'|'COMMAND'|'CONFIG'|string; eventType:string; occurrenceId?:string; incidentId?:string; commandId?:string; deviceId?:string; action?:string; state?:string; objectId?:string; objectRevision?:string; revision?:number; conditionState?:RuleConditionState; ackState?:AlarmAckState; status?:IncidentStatus; actor?:string; comment?:string; conclusion?:string; }
export interface TimelineResponseV2 { items:TimelineItemV2[]; from:number; to:number; instanceId?:string; }

export interface ReplayStreamV2 { role:string; deviceId:string; pointId:string; history:PlatformTelemetryHistoryResponse; }
export interface ReplaySessionV2 { sessionId:string; instanceId:string; contextId:string; deliveryMode:'REPLAY'; configRevision:string; from:number; to:number; createdAt:number; streams:ReplayStreamV2[]; rules?:PlatformRule[]; ruleTransitions?:Record<string,unknown>[]; writesLiveAlarmState?:false; commandAllowed:false; }
export interface ReportCoverageV2 { expectedSamples:number; receivedSamples:number; goodSamples:number; coverage:number|null; }
export interface ReportPointSummaryV2 { role:string; deviceId:string; pointId:string; samplePeriodMs:number; coverage:ReportCoverageV2; counterSegments?:Array<{startTs:number;endTs:number;startValue:number;endValue:number;delta:number;sampleCount:number;gapCount:number}>; }
export interface ReportJobV2 { jobId:string; status:'COMPLETED'|string; createdAt:number; parameters:Record<string,unknown>; summary:{instanceId:string;displayName:string;from:number;to:number;dataCutoff:number;configRevision:string;points:ReportPointSummaryV2[];alarmOccurrences:number;unacknowledgedOccurrences:number;unresolvedCommands:number}; html:string; csv:string; }


export interface CommandPreviewV2 {
  previewId:string; deviceId:string; deviceName:string; action:string; displayName:string; requestedValue:unknown; reason:string;
  requiredPermission:string; executionClass:'NORMAL'|'REALTIME'|'EMERGENCY'|string; parameterSchema:Record<string,unknown>;
  configRevision:string; deviceStatus:string; createdAt:number; expiresAt:number; adapterWriteReady:boolean; adapterDetail:string; contextId?:string;
}
export interface CommandSubmitV2 {
  previewId:string; idempotencyKey:string; commandId?:string; deviceId:string; action:string; requestedValue?:unknown; reason:string; ttlMs?:number; contextId?:string;
}
export interface CommandReconciliationInput { conclusion:'CONFIRMED_APPLIED'|'CONFIRMED_NOT_APPLIED'|'INCONCLUSIVE'; evidence:string; allowRetry?:boolean; }

export interface AgentStatusV2 { enabled:boolean; configured:boolean; provider:string; model:string; apiKeyEnv:string; mode:'MODEL'|'LOCAL'|string; }
export interface AgentHistoryItemV2 { role:'user'|'assistant'; content:string; }
export interface AgentReplyV2 { answer:string; mode:'MODEL'|'LOCAL'|string; provider:string; model:string; generatedAt:number; activeAlarms:number; unresolvedCommands:number; }
