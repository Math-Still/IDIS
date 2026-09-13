import type { Alarm, Device, TelemetryPoint } from '@smart-factory/domain';
import { matchesTwinZoneDevice } from './twin.config';
import type { PlantZoneDefinition, TwinStatus, TwinZoneRuntime } from './twin.types';

function statusRank(status: TwinStatus): number {
  return { normal:0, running:1, maintenance:2, warning:3, offline:4, alarm:5 }[status];
}

export function twinStatusLabel(status: TwinStatus): string {
  return ({ normal:'正常', running:'运行', warning:'注意', alarm:'报警', offline:'离线', maintenance:'维护' } as const)[status];
}

export function buildTwinZoneRuntime(
  zones: PlantZoneDefinition[],
  devices: Device[],
  alarms: Alarm[],
  telemetry: TelemetryPoint[]
): TwinZoneRuntime[] {
  return zones.map((zone) => {
    const zoneDevices = devices.filter((device) => matchesTwinZoneDevice(zone, device));
    const ids = new Set(zoneDevices.map((device) => device.id));
    const activeAlarms = alarms.filter((alarm) => ids.has(alarm.deviceId) && alarm.state !== 'CLEARED');
    const keyPoint = telemetry.find((point) => ids.has(point.deviceId) && typeof point.value === 'number')
      ?? telemetry.find((point) => ids.has(point.deviceId));

    let status: TwinStatus = zoneDevices.length ? 'running' : 'normal';
    const candidates: TwinStatus[] = [];
    if (activeAlarms.some((alarm) => alarm.severity === 'CRITICAL')) candidates.push('alarm');
    else if (activeAlarms.length) candidates.push('warning');
    if (zoneDevices.some((device) => device.status === 'OFFLINE')) candidates.push('offline');
    if (zoneDevices.some((device) => device.status === 'DEGRADED')) candidates.push('warning');
    if (zoneDevices.some((device) => device.status === 'MAINTENANCE')) candidates.push('maintenance');
    if (candidates.length) status = candidates.sort((a,b) => statusRank(b) - statusRank(a))[0];

    return {
      zone,
      status,
      devices: zoneDevices,
      onlineCount: zoneDevices.filter((device) => device.status !== 'OFFLINE').length,
      alarmCount: activeAlarms.length,
      keyPoint
    };
  });
}
