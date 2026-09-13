import type { DataQuality, TelemetryPoint } from './models';
import { classifyTelemetryOrder } from './telemetry-order';

export interface TelemetryTrendCursor {
  seq:number;
  sampleTs:number;
  quality:DataQuality;
}
export type TelemetryTrendOutcome = 'APPEND'|'QUALITY_ONLY'|'DUPLICATE'|'OUT_OF_ORDER'|'SKIP_QUALITY';
export interface TelemetryTrendDecision {
  outcome:TelemetryTrendOutcome;
  cursor:TelemetryTrendCursor;
  breakBefore:boolean;
}

export function evaluateTelemetryTrend(
  previous:TelemetryTrendCursor|undefined,
  point:Pick<TelemetryPoint,'seq'|'sampleTs'|'quality'>,
  gapMs=5000
):TelemetryTrendDecision{
  const cursor={seq:point.seq,sampleTs:point.sampleTs,quality:point.quality};
  const order=classifyTelemetryOrder(previous,point);
  if(order==='OUT_OF_ORDER') return {outcome:'OUT_OF_ORDER',cursor:previous ?? cursor,breakBefore:false};
  if(order==='DUPLICATE'){
    if(previous && previous.quality!==point.quality) return {outcome:'QUALITY_ONLY',cursor:{...previous,quality:point.quality},breakBefore:false};
    return {outcome:'DUPLICATE',cursor:previous ?? cursor,breakBefore:false};
  }
  const recovered=Boolean(previous && (previous.quality==='STALE'||previous.quality==='BAD'));
  const sequenceReset=Boolean(previous && point.sampleTs>previous.sampleTs && point.seq<=previous.seq);
  const gap=Boolean(previous && point.sampleTs-previous.sampleTs>gapMs);
  if(point.quality==='STALE'||point.quality==='BAD') return {outcome:'SKIP_QUALITY',cursor,breakBefore:false};
  return {outcome:'APPEND',cursor,breakBefore:recovered||sequenceReset||gap};
}
