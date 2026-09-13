import type { DataQuality, PointDefinition, TelemetryPoint } from '@smart-factory/domain';
import type { GaugeThresholds, GaugeTone } from './gauge.types';

export function clampGauge(value: number, min: number, max: number): number {
  if (!Number.isFinite(value) || max <= min) return 0;
  return Math.max(0, Math.min(1, (value - min) / (max - min)));
}

export function gaugeThresholds(definition?: PointDefinition): GaugeThresholds {
  return {
    warningLow: definition?.limits?.warningLow,
    warningHigh: definition?.limits?.warningHigh,
    alarmLow: definition?.limits?.alarmLow,
    alarmHigh: definition?.limits?.alarmHigh
  };
}

export function gaugeTone(value: number | null | undefined, quality: DataQuality | undefined, thresholds: GaugeThresholds): GaugeTone {
  if (value === null || value === undefined || !Number.isFinite(value) || quality === 'BAD' || quality === 'STALE') return 'offline';
  if ((thresholds.alarmLow !== undefined && value <= thresholds.alarmLow)
    || (thresholds.alarmHigh !== undefined && value >= thresholds.alarmHigh)) return 'alarm';
  if ((thresholds.warningLow !== undefined && value <= thresholds.warningLow)
    || (thresholds.warningHigh !== undefined && value >= thresholds.warningHigh)
    || quality === 'UNCERTAIN') return 'warning';
  return 'normal';
}

export function gaugeScale(point: TelemetryPoint | undefined, definition?: PointDefinition): { min: number; max: number } {
  const value = typeof point?.value === 'number' && Number.isFinite(point.value) ? point.value : 0;
  const limits = definition?.limits;
  const configured = [limits?.warningLow, limits?.warningHigh, limits?.alarmLow, limits?.alarmHigh]
    .filter((item): item is number => typeof item === 'number' && Number.isFinite(item));

  if (configured.length) {
    const low = Math.min(value, ...configured);
    const high = Math.max(value, ...configured);
    const span = Math.max(1, high - low);
    let min = limits?.alarmLow !== undefined || limits?.warningLow !== undefined ? low - span * 0.18 : Math.min(0, low);
    let max = limits?.alarmHigh !== undefined || limits?.warningHigh !== undefined ? high + span * 0.18 : Math.max(high * 1.25, high + 1);
    if (value >= 0 && configured.every((item) => item >= 0)) min = Math.max(0, min);
    if (min > 0 && (limits?.alarmHigh !== undefined || limits?.warningHigh !== undefined)) min = 0;
    if (max <= min) max = min + 1;
    return { min, max };
  }

  if (point?.unit?.includes('%')) return { min: 0, max: 100 };
  const magnitude = Math.max(Math.abs(value), 1);
  const rounded = Math.pow(10, Math.floor(Math.log10(magnitude)));
  const max = Math.ceil((magnitude * 1.35) / rounded) * rounded;
  return { min: value < 0 ? -max : 0, max: Math.max(max, 1) };
}

export function thresholdSummary(thresholds: GaugeThresholds, unit = ''): string {
  const suffix = unit ? ` ${unit}` : '';
  if (thresholds.warningLow !== undefined && thresholds.warningHigh !== undefined) {
    return `正常 ${thresholds.warningLow}–${thresholds.warningHigh}${suffix}`;
  }
  if (thresholds.warningHigh !== undefined) return `预警 ≥ ${thresholds.warningHigh}${suffix}`;
  if (thresholds.warningLow !== undefined) return `预警 ≤ ${thresholds.warningLow}${suffix}`;
  return '实时监测';
}
