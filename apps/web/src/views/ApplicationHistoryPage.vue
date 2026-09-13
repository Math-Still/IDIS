<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import { useRoute } from 'vue-router';
import { segmentCounter, telemetryCoverage, type TelemetryPoint } from '@smart-factory/domain';
import { useSiteStore } from '../stores/site';
import PageHeader from '../components/PageHeader.vue';
import RealtimeTrendChart, { type TrendSeries } from '../components/RealtimeTrendChart.vue';

const route=useRoute(),store=useSiteStore();const instanceId=computed(()=>String(route.params.id??''));const instance=computed(()=>store.applicationInstances.find(i=>i.instanceId===instanceId.value));
const to=ref(Date.now()),from=ref(to.value-60*60*1000);const rows=ref<Array<{role:string;ref:{deviceId:string;pointId:string};items:TelemetryPoint[];coverage:ReturnType<typeof telemetryCoverage>;segments:ReturnType<typeof segmentCounter>}>>([]);const error=ref('');
function period(deviceId:string,pointId:string){return store.dashboard.devices.find(d=>d.id===deviceId)?.pointDefinitions?.find(p=>p.pointId===pointId)?.samplePeriodMs??1000;}
async function load(){if(!instance.value)return;error.value='';try{rows.value=await Promise.all(Object.entries(instance.value.bindings).map(async([role,r])=>{const h=await store.queryPlatformTelemetryHistory({deviceId:r.deviceId,pointId:r.pointId,from:from.value,to:to.value,limit:10000});const p=period(r.deviceId,r.pointId);return {role,ref:r,items:h.items,coverage:telemetryCoverage(h.items,from.value,to.value,p),segments:role==='totalCount'?segmentCounter(h.items,p*3):[]};}));}catch(e){error.value=e instanceof Error?e.message:String(e);}}
onMounted(async()=>{await store.refreshPlatformConfig().catch(()=>{});await load();});watch(instanceId,load);
function series(row:(typeof rows.value)[number]):TrendSeries[]{return [{key:`${row.ref.deviceId}:${row.ref.pointId}`,label:row.role,unit:row.items[0]?.unit,color:'#126F8C',samples:row.items.filter((x):x is TelemetryPoint&{value:number}=>typeof x.value==='number').map(x=>({ts:x.sampleTs,value:x.value}))}];}
</script>
<template><section class="operator-page"><PageHeader :title="`${instance?.displayName??instanceId} · 历史回溯`" eyebrow="历史记录" :back-to="`/applications/${instanceId}`" back-label="返回应用" description="查看监测数据、数据完整率和历史变化。"/>
<div v-if="error" class="card-empty">{{error}}</div><section v-for="row in rows" :key="row.role" class="hmi-panel"><div class="hmi-panel-head"><div><span>{{row.role}}</span><h2>{{row.ref.deviceId}} : {{row.ref.pointId}}</h2></div><small>覆盖率 {{row.coverage.coverage===null?'—':`${(row.coverage.coverage*100).toFixed(1)}%`}} · 收到 {{row.coverage.receivedSamples}} / 预期 {{row.coverage.expectedSamples}}</small></div><RealtimeTrendChart :series="series(row)" :height="240"/><div v-if="row.segments.length" class="table-wrap"><table class="hmi-table"><thead><tr><th>计数段</th><th>起点</th><th>终点</th><th>增量</th><th>缺口</th></tr></thead><tbody><tr v-for="(s,i) in row.segments" :key="i"><td>{{i+1}}</td><td>{{s.startValue}}</td><td>{{s.endValue}}</td><td>{{s.delta}}</td><td>{{s.gapCount}}</td></tr></tbody></table></div></section></section></template>
