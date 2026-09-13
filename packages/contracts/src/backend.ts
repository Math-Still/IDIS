export interface RealtimeRuntimeConfig {
  staleAfterMs?: number;
  badAfterMs?: number;
  reconnectBaseMs?: number;
  reconnectMaxMs?: number;
  reconnectJitterRatio?: number;
  commandTimeoutMs?: number;
  stableConnectionMs?: number;
  messageSilenceReconnectMs?: number;
}

export interface DeploymentRuntimeConfig {
  platformTarget?: 'generic-browser'|'uos'|'kylin'|'hongzos-openharmony'|string;
  platformLabel?: string;
  hardwareProfile?: string;
  deploymentId?: string;
}

export interface BenchmarkRuntimeConfig {
  enabled?: boolean;
  pingPath?: string;
  iterations?: number;
  warmupIterations?: number;
  intervalMs?: number;
  requestTimeoutMs?: number;
}


export interface TransportRuntimeConfig {
  httpRequestTimeoutMs?: number;
  maxJsonResponseBytes?: number;
  maxRealtimeMessageBytes?: number;
  allowPlaintextForIsolatedLab?: boolean;
}

export interface FaultInjectionRuntimeConfig {
  enabled?: boolean;
  allowInFactory?: boolean;
}

export interface RuntimeConfig {
  apiBaseUrl: string;
  wsBaseUrl: string;
  environment: 'development' | 'test' | 'demo' | 'factory' | string;
  siteId: string;
  backendApiVersion: string;
  dataMode?: 'auto' | 'backend' | 'demo';
  realtime?: RealtimeRuntimeConfig;
  deployment?: DeploymentRuntimeConfig;
  benchmark?: BenchmarkRuntimeConfig;
  faultInjection?: FaultInjectionRuntimeConfig;
  transport?: TransportRuntimeConfig;
}

export interface TelemetryEnvelope<T = unknown> {
  deviceId: string;
  pointId: string;
  value: T;
  unit?: string | null;
  quality: 'GOOD' | 'UNCERTAIN' | 'BAD' | 'STALE' | string;
  sampleTs: number;
  receiveTs?: number;
  source?: string;
  seq?: number;
  sourceEpoch?: string;
  configRevision?: string;
  contextId?: string;
  qualityReasons?: string[];
  provenance?: { originKind:'REAL_DEVICE'|'SIMULATION'|'UNKNOWN'|string; sourceId:string; deliveryMode:'LIVE'|'REPLAY'|string; sourceTimeBasis?:'DEVICE'|'HOST'|'UNKNOWN'|string };
  alarm?: unknown;
  online?: boolean;
}

export interface AuthStatusResponse { authEnabled:boolean; siteName:string; }

export interface BackendRuntimeStatus {
  backend: 'cpp' | string;
  backendVersion: string;
  mode: string;
  realtimeEnabled: boolean;
  realtimeGranted: boolean;
  realtimeDetail: string;
  osAdapter: string;
  deviceAdapter: string;
  deviceAdapterSimulated?: boolean;
  deviceAdapterReady?: boolean;
  deviceAdapterState?: string;
  deviceAdapterDetail?: string;
  deviceRegistryFile?: string;
  configuredDeviceCount?: number;
  emergencyControlEnabled: boolean;
  historianEnabled?: boolean;
  historianDirectory?: string;
  historianTelemetryRecords?: number;
  historianAlarmEvents?: number;
  authEnabled?: boolean;
  auditEnabled?: boolean;
  auditDirectory?: string;
  auditRecords?: number;
  acquisition?: { state:string; detail:string; lastSuccessTs:number; lastFailureTs:number; consecutiveFailures:number; totalFailures:number; pollMs?:number; hmiPublishMs?:number; };
}
