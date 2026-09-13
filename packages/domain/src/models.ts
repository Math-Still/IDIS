export type ScenarioType =
  | 'temperature_humidity'
  | 'pir_lighting'
  | 'hazardous_gas'
  | 'agv_obstacle'
  | 'goods_counting';

export type DeviceStatus = 'ONLINE' | 'OFFLINE' | 'DEGRADED' | 'MAINTENANCE';
export type DataQuality = 'GOOD' | 'UNCERTAIN' | 'BAD' | 'STALE';
export type OriginKind = 'REAL_DEVICE' | 'SIMULATION' | 'UNKNOWN';
export type DeliveryMode = 'LIVE' | 'REPLAY';
export type SourceTimeBasis = 'DEVICE' | 'HOST' | 'UNKNOWN';
export interface DataProvenance { originKind:OriginKind; sourceId:string; deliveryMode:DeliveryMode; sourceTimeBasis?:SourceTimeBasis; }
export type AlarmSeverity = 'INFO' | 'WARNING' | 'CRITICAL';
export type AlarmState = 'ACTIVE' | 'ACKNOWLEDGED' | 'CLEARED';
export type CommandState =
  | 'ISSUED'
  | 'RECEIVED'
  | 'ACCEPTED'
  | 'REJECTED'
  | 'EXECUTING'
  | 'APPLIED'
  | 'FAILED'
  | 'CONFIRMED'
  | 'TIMEOUT'
  | 'EXPIRED'
  | 'OUTCOME_UNKNOWN';

export interface DeviceConnection { interfaceType:string; protocol:string; endpoint?:string; driver?:string; parameters?:Record<string,unknown>; }
export interface PointLimits { warningLow?:number; warningHigh?:number; alarmLow?:number; alarmHigh?:number; setpoint?:number; deadband?:number; }
export interface PointDefinition { pointId:string; label:string; unit?:string; valueType:string; samplePeriodMs:number; limits?:PointLimits; }
export interface CommandCapability { actionId:string; displayName:string; requiredPermission:'command.issue'|'command.emergency'; executionClass:'NORMAL'|'REALTIME'|'EMERGENCY'; reasonRequired:boolean; parameterSchema:Record<string,unknown>; }
export interface Device { id:string; name:string; scenario:ScenarioType; status:DeviceStatus; location:string; protocol:string; connection?:DeviceConnection; adapterId?:string; osNode:string; lastSeen:number; tags:string[]; pointDefinitions?:PointDefinition[]; commandCapabilities?:Record<string,CommandCapability>; provenance?:DataProvenance; configRevision?:string; acquisitionState?:string; acquisitionError?:string; }
export interface TelemetryPoint { deviceId:string; pointId:string; label:string; value:number|string|boolean|null; unit?:string; quality:DataQuality; qualityReasons?:string[]; sampleTs:number; receiveTs:number; seq:number; sourceEpoch?:string; configRevision?:string; contextId?:string; provenance?:DataProvenance; staleSince?:number; acquisitionError?:string; }
export type AlarmSourceDomain = 'FIELD'|'PLATFORM_ACQUISITION'|'PLATFORM_STORAGE'|'RULE'|'UNKNOWN';
export interface Alarm {
  id:string; deviceId:string; title:string; message:string; severity:AlarmSeverity; state:AlarmState; raisedAt:number;
  occurrenceId?:string; sourceDomain?:AlarmSourceDomain;
  acknowledgedAt?:number; acknowledgedBy?:string; acknowledgementComment?:string;
  clearedAt?:number; clearedBy?:string; clearComment?:string;
}
export type AlarmLifecycleEventType = 'RAISED' | 'ACKNOWLEDGED' | 'CLEARED';
export interface AlarmLifecycleEvent {
  eventId:string; alarmId:string; deviceId:string; eventType:AlarmLifecycleEventType; timestamp:number; source:string; occurrenceId?:string;
  operator?:string; comment?:string; alarm:Alarm;
}
export interface AlarmActionRequest { operator?:string; comment?:string; }
export interface TelemetryHistoryResponse { enabled:boolean; items:TelemetryPoint[]; totalMatched:number; limit:number; from:number; to:number; truncated:boolean; }
export interface AlarmHistoryResponse { enabled:boolean; items:AlarmLifecycleEvent[]; totalMatched:number; limit:number; from:number; to:number; truncated:boolean; }
export type CommandFeedbackStatus = 'PENDING'|'CONFIRMED'|'FAILED'|'TIMEOUT'|'UNKNOWN'|'NOT_APPLIED';
export type CommandDeliveryCertainty = 'NOT_SENT'|'REJECTED'|'POSSIBLY_APPLIED'|'APPLIED';
export interface CommandEvidence { type:string; detail:string; timestamp:number; observedState?:CommandState; reconciliationId?:string; conclusion?:string; evidence?:string; allowRetry?:boolean; actorId?:string; }
export interface CommandReconciliation { reconciliationId:string; actorId:string; conclusion:'CONFIRMED_APPLIED'|'CONFIRMED_NOT_APPLIED'|'INCONCLUSIVE'; evidence:string; allowRetry:boolean; createdAt:number; }
export interface Command { id:string; deviceId:string; action:string; requestedValue?:unknown; state:CommandState; revision?:number; issuedAt:number; updatedAt:number; expiresAt?:number; operator?:string; operatorRole?:UserRole; reason?:string; lastEventSeq?:number; detail?:string; feedbackStatus?:CommandFeedbackStatus; feedbackAt?:number; deliveryCertainty?:CommandDeliveryCertainty; executionSupervision?:string; adapterQuarantined?:boolean; previewId?:string; idempotencyKey?:string; configRevision?:string; idempotencyExpiresAt?:number; observations?:CommandEvidence[]; reconciliations?:CommandReconciliation[]; }
export interface CommandRequest { commandId?:string; deviceId:string; action:string; requestedValue?:unknown; ttlMs?:number; operator?:string; reason?:string; }
export interface CommandEvent { commandId:string; state:CommandState; timestamp:number; seq?:number; revision?:number; detail?:string; feedbackStatus?:CommandFeedbackStatus; feedbackAt?:number; deliveryCertainty?:CommandDeliveryCertainty; executionSupervision?:string; adapterQuarantined?:boolean; }
export interface CommunicationHealth { deviceId:string; online:boolean; quality:DataQuality; latencyMsP50?:number; latencyMsP95?:number; latencyMsP99?:number; jitterMs?:number; messageRatePerMin?:number; reconnectCount?:number; errorCount?:number; lastSeen?:number; provenance?:DataProvenance; configRevision?:string; acquisitionState?:string; acquisitionError?:string; }
export interface ScenarioSummary { type:ScenarioType; label:string; description:string; deviceCount:number; onlineCount:number; activeAlarmCount:number; }
export interface DashboardSnapshot { siteName:string; generatedAt:number; provenance:DataProvenance; devices:Device[]; telemetry:TelemetryPoint[]; alarms:Alarm[]; commands:Command[]; communication:CommunicationHealth[]; scenarios:ScenarioSummary[]; }

export type UserRole = 'OBSERVER'|'OPERATOR'|'ENGINEER'|'ADMINISTRATOR';
export type Permission = 'view'|'alarm.ack'|'command.issue'|'command.emergency'|'command.reconcile'|'audit.view'|'runtime.inspect'|'config.edit'|'config.publish'|'rule.manage'|'incident.assign'|'incident.resolve'|'incident.close'|'report.export';
export interface AuthUser { id:string; displayName:string; role:UserRole; authenticated:boolean; permissions:Permission[]; }
export interface AuthSessionResponse { authEnabled:boolean; user:AuthUser; }
export interface AuditEvent { eventId:string; timestamp:number; actorId:string; actorDisplayName?:string; actorRole:UserRole|string; action:string; resourceType:string; resourceId:string; outcome:string; detail:string; reason?:string; remoteAddress?:string; }
export interface AuditHistoryResponse { enabled:boolean; items:AuditEvent[]; totalMatched:number; limit:number; from:number; to:number; truncated:boolean; }
