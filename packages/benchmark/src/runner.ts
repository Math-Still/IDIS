import type{BenchmarkOptions,BenchmarkSample}from'./models';
export type BenchmarkTask=(seq:number)=>Promise<void>;
const sleep=(ms:number)=>new Promise<void>(resolve=>setTimeout(resolve,ms));
export async function runSequentialBenchmark(task:BenchmarkTask,options:BenchmarkOptions,onSample?:(s:BenchmarkSample)=>void):Promise<BenchmarkSample[]>{
  for(let i=0;i<Math.max(0,options.warmupIterations);i++){try{await task(-1-i);}catch{/* warmup never enters evidence */}if(options.intervalMs>0)await sleep(options.intervalMs);}
  const samples:BenchmarkSample[]=[];
  for(let seq=1;seq<=Math.max(1,options.iterations);seq++){
    const startedAt=Date.now();const start=performance.now();let sample:BenchmarkSample;
    try{await task(seq);sample={seq,success:true,latencyMs:performance.now()-start,startedAt,completedAt:Date.now()};}
    catch(error){sample={seq,success:false,latencyMs:null,startedAt,completedAt:Date.now(),error:error instanceof Error?error.message:String(error)};}
    samples.push(sample);onSample?.(sample);if(options.intervalMs>0&&seq<options.iterations)await sleep(options.intervalMs);
  }
  return samples;
}
