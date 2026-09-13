import type { DataQuality, TelemetryPoint } from '@smart-factory/domain';

export type IndustrialValueStatus = 'VALUE' | 'NO_DATA' | 'INVALID' | 'FROZEN' | 'OFFLINE' | 'UNKNOWN';

export interface FormattedIndustrialValue {
  text: string;
  valueText: string;
  unitText: string;
  status: IndustrialValueStatus;
}

const NBSP = '\u00A0';

const STATUS_TEXT: Record<Exclude<IndustrialValueStatus, 'VALUE'>, string> = {
  NO_DATA: '---',
  INVALID: 'INVALID',
  FROZEN: 'FROZEN',
  OFFLINE: 'OFFLINE',
  UNKNOWN: 'UNKNOWN'
};

function normalized(value?: string): string {
  return (value ?? '').trim().toLowerCase().replaceAll(' ', '');
}

function decimalsFor(unit?: string, pointId?: string, label?: string, value?: number): number {
  const u = normalized(unit);
  const key = `${normalized(pointId)}|${normalized(label)}`;

  if (/count|counter|total|quantity|数量|计数|累计/.test(key)) return 0;
  if (u === '件' || u === 'pcs' || u === 'piece' || u === 'count') return 0;
  if (u.includes('°c') || u.includes('℃') || /temperature|温度/.test(key)) return 1;
  if (u === '%' || /humidity|湿度|percent|百分比/.test(key)) return 1;
  if (u === 'ppm' || u === 'ppb' || /gas|气体|浓度/.test(key)) return 2;
  if (u === 'cm' || u === 'mm' || /distance|距离/.test(key)) return 1;
  if (u === 'm/s' || u === 'mps' || /speed|速度/.test(key)) return 2;
  if (u === 'ms' || /latency|jitter|延迟|抖动/.test(key)) return 1;
  if (u.includes('/min') || /rate|速率/.test(key)) return 1;
  if (u === 'v' || u === 'a' || u === 'kw' || u === 'mw') return 2;

  const abs = Math.abs(value ?? 0);
  if (abs >= 1000) return 0;
  if (abs >= 100) return 1;
  return 2;
}

export function formatNumber(value: unknown, options: {
  unit?: string;
  pointId?: string;
  label?: string;
  decimals?: number;
  fallback?: string;
} = {}): string {
  if (value === null || value === undefined || value === '') return options.fallback ?? '---';
  const numeric = typeof value === 'number' ? value : Number(value);
  if (!Number.isFinite(numeric)) return options.fallback ?? '---';
  const decimals = Math.max(0, Math.min(4, options.decimals ?? decimalsFor(options.unit, options.pointId, options.label, numeric)));
  return numeric.toLocaleString('zh-CN', {
    minimumFractionDigits: decimals,
    maximumFractionDigits: decimals,
    useGrouping: Math.abs(numeric) >= 1000
  });
}

export function formatInteger(value: unknown, fallback = '---'): string {
  const numeric = typeof value === 'number' ? value : Number(value);
  if (!Number.isFinite(numeric)) return fallback;
  return Math.round(numeric).toLocaleString('zh-CN', { maximumFractionDigits: 0 });
}

export function formatDurationSeconds(msOrTimestamp: unknown, now = Date.now()): string {
  const numeric = typeof msOrTimestamp === 'number' ? msOrTimestamp : Number(msOrTimestamp);
  if (!Number.isFinite(numeric)) return '---';
  const seconds = Math.max(0, Math.round((now - numeric) / 1000));
  if (seconds < 60) return `${seconds}${NBSP}s`;
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes}${NBSP}min`;
  const hours = Math.floor(minutes / 60);
  return `${hours}${NBSP}h`;
}

export function formatLatency(value: unknown): string {
  const formatted = formatNumber(value, { unit: 'ms', decimals: 1 });
  return formatted === '---' ? formatted : `${formatted}${NBSP}ms`;
}

export function formatRate(value: unknown): string {
  const formatted = formatNumber(value, { unit: '/min', decimals: 1 });
  return formatted === '---' ? formatted : `${formatted}${NBSP}/min`;
}

function qualityStatus(quality: DataQuality): Exclude<IndustrialValueStatus, 'VALUE'> | null {
  if (quality === 'BAD') return 'INVALID';
  if (quality === 'STALE') return 'FROZEN';
  return null;
}

export function formatTelemetry(point?: TelemetryPoint | null): FormattedIndustrialValue {
  if (!point) return { text:'---', valueText:'---', unitText:'', status:'NO_DATA' };
  const status = qualityStatus(point.quality);
  if (status) {
    const text = STATUS_TEXT[status];
    return { text, valueText:text, unitText:'', status };
  }
  if (point.value === true) return { text:'ON', valueText:'ON', unitText:'', status:'VALUE' };
  if (point.value === false) return { text:'OFF', valueText:'OFF', unitText:'', status:'VALUE' };
  if (typeof point.value === 'number') {
    if (!Number.isFinite(point.value)) return { text:'INVALID', valueText:'INVALID', unitText:'', status:'INVALID' };
    const valueText = formatNumber(point.value, { unit:point.unit, pointId:point.pointId, label:point.label });
    const unitText = (point.unit ?? '').trim();
    return { text: unitText ? `${valueText}${NBSP}${unitText}` : valueText, valueText, unitText, status:'VALUE' };
  }
  const text = safeDisplayText(point.value, '---', 32);
  if (text === '---') return { text, valueText:text, unitText:'', status:'NO_DATA' };
  return { text, valueText:text, unitText:'', status:'VALUE' };
}

export function formatTelemetryText(point?: TelemetryPoint | null): string {
  return formatTelemetry(point).text;
}

export function safeDisplayText(value: unknown, fallback = '---', maxLength = 80): string {
  if (value === null || value === undefined) return fallback;
  const text = String(value).replace(/\s+/g, ' ').trim();
  if (!text) return fallback;
  if (text.length <= maxLength) return text;
  return `${text.slice(0, Math.max(1, maxLength - 1))}…`;
}

export function publicSiteLabel(siteName: string): string {
  return safeDisplayText(siteName, '智慧工厂安全监测控制平台', 40);
}
