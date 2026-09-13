<script setup lang="ts">
import { computed } from 'vue';
import { useRoute } from 'vue-router';
import { deviceProtocol, scenarioLabels, severityLabel, type ScenarioType, type TelemetryPoint } from '@smart-factory/domain';
import { useSiteStore } from '../stores/site';
import PageHeader from '../components/PageHeader.vue';
import StatusPill from '../components/StatusPill.vue';
import SourceQualityBadge from '../components/SourceQualityBadge.vue';
import RealtimeTrendChart, { type TrendSeries } from '../components/RealtimeTrendChart.vue';
import ScenarioProcessPanel from '../components/ScenarioProcessPanel.vue';
import EventTimeline from '../components/EventTimeline.vue';
import { useRealtimeTrendHistory } from '../composables/useRealtimeTrendHistory';
import { formatInteger, formatLatency, formatTelemetryText } from '../utils/industrial-format';
import { alarmStateLabel } from '../utils/operator-labels';
import { trendColor } from '../utils/scenario-colors';

const route=useRoute(); const store=useSiteStore();
const instanceId=computed(()=>String(route.params.id??''));
const instance=computed(()=>store.applicationInstances.find(item=>item.instanceId===instanceId.value)??null);
const type=computed(()=>(instance.value?.templateId??'temperature_humidity') as ScenarioType);
const refs=computed(()=>Object.entries(instance.value?.bindings??{}));
const deviceIds=computed(()=>new Set([...refs.value.map(([,ref])=>ref.deviceId),...(instance.value?.allowedActions??[]).map(item=>item.deviceId)]));
const devices=computed(()=>store.dashboard.devices.filter(device=>deviceIds.value.has(device.id)));
const telemetry=computed(()=>store.dashboard.telemetry.filter(point=>refs.value.some(([,ref])=>ref.deviceId===point.deviceId&&ref.pointId===point.pointId)));
const alarms=computed(()=>store.dashboard.alarms.filter(alarm=>deviceIds.value.has(alarm.deviceId)).sort((a,b)=>b.raisedAt-a.raisedAt));
const comm=computed(()=>store.dashboard.communication.filter(item=>deviceIds.value.has(item.deviceId)));
const numeric=computed(()=>telemetry.value.filter((point):point is TelemetryPoint & {value:number}=>typeof point.value==='number'));
const {history}=useRealtimeTrendHistory(numeric,{maxSamples:180,gapMs:5000});
const assetName=computed(()=>store.platformAssets.find(asset=>asset.id===instance.value?.assetId)?.name??instance.value?.assetId??'未指定区域');
const grouped=computed(()=>{const map=new Map<string,TelemetryPoint[]>();for(const point of numeric.value){const key=point.unit??'value';map.set(key,[...(map.get(key)??[]),point]);}return [...map.entries()].slice(0,2);});
const roleLabels:Record<string,string>={ambientTemperature:'温度',ambientHumidity:'湿度',fanState:'通风状态',occupied:'人员感应',lightState:'照明状态',lightMode:'控制模式',gasConcentration:'气体浓度',obstacleDistance:'障碍距离',motionState:'车辆状态',speed:'速度',position:'位置',task:'当前任务',totalCount:'累计计数',countRate:'当前速率',target:'目标值'};
function roleLabel(role:string){return roleLabels[role]??role;}
function series(points:TelemetryPoint[]):TrendSeries[]{return points.slice(0,3).map(point=>({key:`${point.deviceId}:${point.pointId}`,label:point.label,unit:point.unit,color:trendColor(type.value,point.pointId),samples:history.value[`${point.deviceId}:${point.pointId}`]??[]}));}
function severityTone(severity:string):'info'|'warn'|'bad'{return severity==='CRITICAL'?'bad':severity==='WARNING'?'warn':'info';}
function bindingPoint(deviceId:string,pointId:string){return store.dashboard.telemetry.find(p=>p.deviceId===deviceId&&p.pointId===pointId);}
function bindingValue(deviceId:string,pointId:string){return formatTelemetryText(bindingPoint(deviceId,pointId));}
</script>

<template>
<section class="operator-page scenario-business-page" :data-scenario="type">
  <template v-if="instance">
    <PageHeader :title="instance.displayName" :eyebrow="scenarioLabels[type]??'运行监测'" back-to="/applications" back-label="返回监测应用" :description="`${assetName} · ${devices.length} 台相关设备`">
      <template #actions><RouterLink class="btn-secondary link-button" :to="`/applications/${instance.instanceId}/history`">历史记录</RouterLink><RouterLink class="btn-secondary link-button" :to="`/applications/${instance.instanceId}/replay`">历史回放</RouterLink></template>
    </PageHeader>

    <ScenarioProcessPanel :type="type" :devices="devices" :telemetry="telemetry" :communication="comm" :alarms="alarms" :bindings="instance.bindings" :strict-bindings="true" />

    <section class="hmi-panel application-points-panel">
      <div class="hmi-panel-head"><div><span>当前测点</span><h2>监测数据</h2></div><small>{{refs.length}} 项</small></div>
      <div class="table-wrap"><table class="hmi-table"><thead><tr><th>监测项目</th><th>设备</th><th>测点</th><th>当前值</th><th>状态</th></tr></thead><tbody>
        <tr v-for="([role,ref]) in refs" :key="role"><td><strong>{{roleLabel(role)}}</strong></td><td>{{store.dashboard.devices.find(d=>d.id===ref.deviceId)?.name??ref.deviceId}}</td><td>{{bindingPoint(ref.deviceId,ref.pointId)?.label??ref.pointId}}</td><td>{{bindingValue(ref.deviceId,ref.pointId)}}</td><td><SourceQualityBadge :point="bindingPoint(ref.deviceId,ref.pointId)"/></td></tr>
      </tbody></table></div>
    </section>

    <section class="hmi-panel scenario-trend-panel"><div class="hmi-panel-head"><div><span>实时趋势</span><h2>关键参数变化</h2></div><small>{{grouped.length?'持续更新':'暂无数值数据'}}</small></div><div v-if="grouped.length" class="scenario-trend-grid"><div v-for="([unit,points],index) in grouped" :key="unit" class="scenario-trend-item"><RealtimeTrendChart :series="series(points)" :height="index===0?300:240"/></div></div><div v-else class="card-empty">当前没有可绘制的数值数据</div></section>

    <section class="hmi-panel"><div class="hmi-panel-head"><div><span>运行记录</span><h2>事件时间线</h2></div><small>报警 · 处置 · 操作</small></div><EventTimeline :instance-id="instance.instanceId" /></section>

    <div class="scenario-bottom-grid">
      <section class="hmi-panel scenario-communication-panel"><div class="hmi-panel-head"><div><span>设备通信</span><h2>连接状态</h2></div><RouterLink to="/communication">查看全部</RouterLink></div><div class="comm-diagnostic-list comm-diagnostic-list-wide"><div v-for="item in comm" :key="item.deviceId" :data-quality="item.quality.toLowerCase()"><span class="hmi-state-indicator"><i></i></span><div><strong>{{devices.find(d=>d.id===item.deviceId)?.name??item.deviceId}}</strong><small>{{devices.find(d=>d.id===item.deviceId)?deviceProtocol(devices.find(d=>d.id===item.deviceId)!):'—'}}</small></div><div><b>P95 {{formatLatency(item.latencyMsP95)}}</b><span>错误 {{formatInteger(item.errorCount)}}</span></div></div><div v-if="!comm.length" class="card-empty">暂无通信状态</div></div></section>
      <section class="hmi-panel scenario-alarm-panel"><div class="hmi-panel-head"><div><span>相关报警</span><h2>报警状态</h2></div><RouterLink to="/alarms">报警中心</RouterLink></div><div class="scenario-alarm-list"><div v-for="alarm in alarms.slice(0,8)" :key="alarm.id" class="scenario-alarm-row" :data-severity="alarm.severity.toLowerCase()"><StatusPill :tone="severityTone(alarm.severity)" :label="severityLabel[alarm.severity]"/><div><strong>{{alarm.title}}</strong><small>{{new Date(alarm.raisedAt).toLocaleString('zh-CN',{hour12:false})}} · {{alarmStateLabel[alarm.state]}}</small></div></div><div v-if="!alarms.length" class="quiet-inline-state"><span>✓</span><strong>当前无相关报警</strong></div></div></section>
    </div>
  </template>
  <div v-else class="hmi-panel card-empty">当前监测应用不可用</div>
</section>
</template>
