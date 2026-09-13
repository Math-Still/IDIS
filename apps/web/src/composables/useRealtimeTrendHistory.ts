import { ref, watch, type Ref, type ComputedRef } from 'vue';
import { evaluateTelemetryTrend, type TelemetryPoint, type TelemetryTrendCursor } from '@smart-factory/domain';
import type { TrendSample } from '../trend-types';

type TelemetrySource = Ref<TelemetryPoint[]> | ComputedRef<TelemetryPoint[]>;

export interface RealtimeTrendHistoryOptions {
  maxSamples?:number;
  gapMs?:number;
}

/**
 * Builds browser-side live trend history strictly from new telemetry samples.
 * It never re-samples the current snapshot on a timer. Duplicate/out-of-order
 * values are ignored. STALE/BAD periods do not create synthetic flat lines;
 * the first recovered sample starts a new segment.
 */
export function useRealtimeTrendHistory(source:TelemetrySource, options:RealtimeTrendHistoryOptions={}) {
  const maxSamples=Math.max(10, options.maxSamples ?? 180);
  const gapMs=Math.max(500, options.gapMs ?? 5000);
  const history=ref<Record<string,TrendSample[]>>({});
  const cursors=new Map<string,TelemetryTrendCursor>();
  const acceptedSamples=ref(0);
  const ignoredDuplicates=ref(0);
  const ignoredOutOfOrder=ref(0);

  watch(source,(points)=>{
    const next={...history.value};
    for(const point of points){
      if(typeof point.value!=='number' || !Number.isFinite(point.value)) continue;
      const key=`${point.deviceId}:${point.pointId}`;
      const decision=evaluateTelemetryTrend(cursors.get(key),point,gapMs);
      if(decision.outcome==='OUT_OF_ORDER'){ ignoredOutOfOrder.value+=1; continue; }
      if(decision.outcome==='DUPLICATE'){ ignoredDuplicates.value+=1; continue; }
      cursors.set(key,decision.cursor);
      if(decision.outcome==='QUALITY_ONLY'||decision.outcome==='SKIP_QUALITY') continue;
      const sample:TrendSample={ts:point.sampleTs,value:point.value,breakBefore:decision.breakBefore};
      next[key]=[...(next[key]??[]),sample].slice(-maxSamples);
      acceptedSamples.value+=1;
    }
    history.value=next;
  },{immediate:true,deep:true});

  return { history, acceptedSamples, ignoredDuplicates, ignoredOutOfOrder };
}
