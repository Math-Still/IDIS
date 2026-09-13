import type { TelemetryPoint } from './models';

export interface CoverageSummary {
  expectedSamples:number;
  receivedSamples:number;
  goodSamples:number;
  coverage:number|null;
}
export interface CounterSegment {
  startTs:number; endTs:number; startValue:number; endValue:number; delta:number; sampleCount:number; gapCount:number;
}

export function telemetryCoverage(items:TelemetryPoint[], from:number, to:number, samplePeriodMs:number):CoverageSummary {
  if(samplePeriodMs<=0||to<from)return {expectedSamples:0,receivedSamples:items.length,goodSamples:items.filter(i=>i.quality==='GOOD').length,coverage:null};
  const expected=Math.max(1,Math.floor((to-from)/samplePeriodMs)+1);
  return {expectedSamples:expected,receivedSamples:items.length,goodSamples:items.filter(i=>i.quality==='GOOD').length,coverage:Math.min(1,items.length/expected)};
}

export function segmentCounter(items:TelemetryPoint[], maxGapMs=0):CounterSegment[] {
  const samples=items.filter((item):item is TelemetryPoint&{value:number}=>typeof item.value==='number').slice().sort((a,b)=>a.sampleTs-b.sampleTs);
  if(!samples.length)return [];
  const result:CounterSegment[]=[]; let start=0; let gaps=0;
  const flush=(end:number)=>{const a=samples[start],b=samples[end];result.push({startTs:a.sampleTs,endTs:b.sampleTs,startValue:a.value,endValue:b.value,delta:Math.max(0,b.value-a.value),sampleCount:end-start+1,gapCount:gaps});};
  for(let i=1;i<samples.length;i++){
    if(maxGapMs>0&&samples[i].sampleTs-samples[i-1].sampleTs>maxGapMs)gaps++;
    if(samples[i].value<samples[i-1].value){flush(i-1);start=i;gaps=0;}
  }
  flush(samples.length-1); return result;
}
