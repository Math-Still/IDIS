export type BenchmarkMode='SYNTHETIC'|'BACKEND_HTTP_PING';
export type EvidenceLevel='DEMO_ONLY'|'MEASURED'|'UNAVAILABLE';

export interface BenchmarkEnvironment {
  platform:string;
  platformLabel:string;
  osVersion:string|null;
  deviceModel:string|null;
  architecture:string|null;
  hardwareProfile:string;
  deploymentId:string;
  webVersion:string;
  shellVersion:string|null;
  bridgeVersion:string;
  backendApiVersion:string;
}

export interface BenchmarkSample {
  seq:number;
  success:boolean;
  latencyMs:number|null;
  startedAt:number;
  completedAt:number;
  error?:string;
}

export interface BenchmarkSummary {
  total:number;
  success:number;
  errors:number;
  successRate:number;
  minMs:number|null;
  meanMs:number|null;
  p50Ms:number|null;
  p95Ms:number|null;
  p99Ms:number|null;
  maxMs:number|null;
  jitterStdDevMs:number|null;
  throughputPerSec:number;
  durationMs:number;
}

export interface BenchmarkReport {
  id:string;
  createdAt:number;
  mode:BenchmarkMode;
  evidenceLevel:EvidenceLevel;
  target:string;
  environment:BenchmarkEnvironment;
  summary:BenchmarkSummary;
  samples:BenchmarkSample[];
  notes:string[];
}

export interface BenchmarkOptions {
  iterations:number;
  warmupIterations:number;
  intervalMs:number;
}
