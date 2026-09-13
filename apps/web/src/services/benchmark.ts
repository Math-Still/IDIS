import{BRIDGE_PROTOCOL_VERSION,WEB_APP_VERSION,type RuntimeConfig}from'@smart-factory/contracts';
import{runSequentialBenchmark,summarizeSamples,type BenchmarkEnvironment,type BenchmarkReport}from'@smart-factory/benchmark';
import type{PlatformAdapter}from'@smart-factory/platform';

async function fetchWithTimeout(url:string,ms:number):Promise<Response>{
  const controller=new AbortController();
  const timer=setTimeout(()=>controller.abort(),ms);
  try{return await fetch(url,{method:'GET',cache:'no-store',headers:{Accept:'application/json'},signal:controller.signal});}
  catch(error){if(error instanceof Error&&error.name==='AbortError')throw new Error(`request timeout after ${ms} ms`);throw error;}
  finally{clearTimeout(timer);}
}

async function environment(platform:PlatformAdapter,config:RuntimeConfig):Promise<BenchmarkEnvironment>{
  const[runtime,device]=await Promise.all([platform.getRuntimeInfo(),platform.getDeviceInfo()]);
  let shellVersion:string|null=null;
  try{const hs=await platform.invokeNativeCapability<{shellVersion?:string|null}>('bridge.handshake',{});shellVersion=hs?.shellVersion??null;}catch{/* browser */}
  return{platform:runtime.platform,platformLabel:config.deployment?.platformLabel??runtime.platform,osVersion:device.osVersion,deviceModel:device.model,architecture:device.architecture,hardwareProfile:config.deployment?.hardwareProfile??'not-recorded',deploymentId:config.deployment?.deploymentId??config.siteId,webVersion:WEB_APP_VERSION,shellVersion,bridgeVersion:BRIDGE_PROTOCOL_VERSION,backendApiVersion:config.backendApiVersion};
}

export async function runSyntheticBenchmark(platform:PlatformAdapter,config:RuntimeConfig,onProgress?:(done:number,total:number)=>void):Promise<BenchmarkReport>{
  const opts=config.benchmark??{};const iterations=opts.iterations??30;let done=0;
  const samples=await runSequentialBenchmark(async(seq)=>{const delay=4+(Math.abs(seq*17)%17);await new Promise(resolve=>setTimeout(resolve,delay));},{iterations,warmupIterations:opts.warmupIterations??3,intervalMs:opts.intervalMs??50},()=>{done+=1;onProgress?.(done,iterations);});
  return{id:`synthetic-${Date.now()}`,createdAt:Date.now(),mode:'SYNTHETIC',evidenceLevel:'DEMO_ONLY',target:'local timer workload',environment:await environment(platform,config),summary:summarizeSamples(samples),samples,notes:['Synthetic mode validates statistics/UI only. It is not cross-OS communication performance evidence.','Do not use this result to rank UOS, Kylin, HongZOS or OpenHarmony.']};
}

export async function runBackendPingBenchmark(platform:PlatformAdapter,config:RuntimeConfig,onProgress?:(done:number,total:number)=>void):Promise<BenchmarkReport>{
  const opts=config.benchmark??{};const iterations=opts.iterations??30;const timeout=opts.requestTimeoutMs??5000;const base=config.apiBaseUrl.replace(/\/$/,'');const path=(opts.pingPath??'benchmark/ping').replace(/^\//,'');const target=`${base}/${config.backendApiVersion}/${path}`;let done=0;
  const samples=await runSequentialBenchmark(async(seq)=>{const response=await fetchWithTimeout(`${target}?seq=${seq}&t=${Date.now()}`,timeout);if(!response.ok)throw new Error(`HTTP ${response.status}`);const text=await response.text();if(text.length>64*1024)throw new Error('benchmark response exceeds 64 KiB');},{iterations,warmupIterations:opts.warmupIterations??3,intervalMs:opts.intervalMs??100},()=>{done+=1;onProgress?.(done,iterations);});
  return{id:`http-${Date.now()}`,createdAt:Date.now(),mode:'BACKEND_HTTP_PING',evidenceLevel:'MEASURED',target,environment:await environment(platform,config),summary:summarizeSamples(samples),samples,notes:['Measures Web Core/Shell to configured backend HTTP round-trip under the current platform combination.','Hardware, network, backend build and payload must be held constant before comparing operating systems.','This is not Modbus polling latency and not MQTT broker latency.']};
}

export function downloadBenchmarkReport(report:BenchmarkReport){const blob=new Blob([JSON.stringify(report,null,2)],{type:'application/json'});const url=URL.createObjectURL(blob);const a=document.createElement('a');a.href=url;a.download=`benchmark-${report.mode.toLowerCase()}-${report.createdAt}.json`;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
