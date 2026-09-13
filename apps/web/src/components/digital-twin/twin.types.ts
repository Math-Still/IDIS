import type { Device, TelemetryPoint } from '@smart-factory/domain';

export type TwinStatus = 'normal' | 'running' | 'warning' | 'alarm' | 'offline' | 'maintenance';
export type TwinShape = 'warehouse' | 'smelter' | 'converter' | 'refining' | 'acid' | 'utilities' | 'casting';
export type TwinLabelPriority = 'primary' | 'secondary';

export interface PlantZoneDefinition {
  id: string;
  name: string;
  shortName: string;
  description: string;
  shape: TwinShape;
  position: [number, number];
  size: [number, number, number];
  labelPosition: [number, number];
  labelPriority: TwinLabelPriority;
  deviceIds?: string[];
  locationKeywords?: string[];
}

export interface ProcessRouteDefinition {
  id: string;
  from: string;
  to: string;
  kind: 'material' | 'gas' | 'product' | 'utility';
  path: string;
}

export interface TwinZoneRuntime {
  zone: PlantZoneDefinition;
  status: TwinStatus;
  devices: Device[];
  onlineCount: number;
  alarmCount: number;
  keyPoint?: TelemetryPoint;
}
