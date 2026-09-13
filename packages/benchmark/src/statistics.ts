import type{BenchmarkSample,BenchmarkSummary}from'./models';
function round(v:number){return Math.round(v*1000)/1000;}
export function percentile(values:number[],p:number):number|null{
  if(!values.length)return null;
  const sorted=[...values].sort((a,b)=>a-b);
  const rank=(sorted.length-1)*Math.min(1,Math.max(0,p));
  const lo=Math.floor(rank),hi=Math.ceil(rank),w=rank-lo;
  return round(sorted[lo]*(1-w)+sorted[hi]*w);
}
export function summarizeSamples(samples:BenchmarkSample[]):BenchmarkSummary{
  if(!samples.length)return{total:0,success:0,errors:0,successRate:0,minMs:null,meanMs:null,p50Ms:null,p95Ms:null,p99Ms:null,maxMs:null,jitterStdDevMs:null,throughputPerSec:0,durationMs:0};
  const ok=samples.filter(s=>s.success&&typeof s.latencyMs==='number');
  const values=ok.map(s=>s.latencyMs as number);
  const start=Math.min(...samples.map(s=>s.startedAt));const end=Math.max(...samples.map(s=>s.completedAt));const duration=Math.max(0,end-start);
  const mean=values.length?values.reduce((a,b)=>a+b,0)/values.length:null;
  const variance=values.length&&mean!==null?values.reduce((a,b)=>a+Math.pow(b-mean,2),0)/values.length:null;
  return{
    total:samples.length,success:ok.length,errors:samples.length-ok.length,successRate:round(ok.length/samples.length),
    minMs:values.length?round(Math.min(...values)):null,meanMs:mean===null?null:round(mean),p50Ms:percentile(values,.5),p95Ms:percentile(values,.95),p99Ms:percentile(values,.99),maxMs:values.length?round(Math.max(...values)):null,
    jitterStdDevMs:variance===null?null:round(Math.sqrt(variance)),throughputPerSec:duration>0?round(ok.length/(duration/1000)):0,durationMs:Math.round(duration)
  };
}
