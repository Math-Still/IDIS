<script setup lang="ts">
import { computed } from 'vue';
import { qualityLabel, scenarioDescriptions, scenarioLabels, type DataQuality, type ScenarioType } from '@smart-factory/domain';
import { useSiteStore } from '../stores/site';
import PageHeader from '../components/PageHeader.vue';
import AppIcon from '../components/AppIcon.vue';
import { formatTelemetryText } from '../utils/industrial-format';
const store=useSiteStore();
const configuredRows=computed(()=>store.applicationInstances.map((instance)=>{
  const refs=Object.values(instance.bindings);
  const deviceIds=new Set(refs.map((ref)=>ref.deviceId));
  const devices=store.dashboard.devices.filter((d)=>deviceIds.has(d.id));
  const telemetry=store.dashboard.telemetry.filter((p)=>refs.some((ref)=>ref.deviceId===p.deviceId&&ref.pointId===p.pointId));
  const key=telemetry.find((p)=>typeof p.value==='number')??telemetry[0];
  const comm=store.dashboard.communication.filter((c)=>deviceIds.has(c.deviceId));
  const alarms=store.dashboard.alarms.filter((a)=>deviceIds.has(a.deviceId)&&a.state!=='CLEARED');
  const quality:DataQuality=comm.some(c=>c.quality==='BAD')?'BAD':comm.some(c=>c.quality==='UNCERTAIN')?'UNCERTAIN':comm.some(c=>c.quality==='STALE')?'STALE':'GOOD';
  return {instance,devices,key,quality,activeAlarmCount:alarms.length,onlineCount:devices.filter(d=>d.status!=='OFFLINE').length};
}));
const legacyRows=computed(()=>store.dashboard.scenarios.map((s)=>{
  const devices=store.dashboard.devices.filter(d=>d.scenario===s.type);
  const telemetry=store.dashboard.telemetry.filter(p=>devices.some(d=>d.id===p.deviceId));
  const key=telemetry.find(p=>typeof p.value==='number')??telemetry[0];
  const comm=store.dashboard.communication.filter(c=>devices.some(d=>d.id===c.deviceId));
  const quality:DataQuality=comm.some(c=>c.quality==='BAD')?'BAD':comm.some(c=>c.quality==='UNCERTAIN')?'UNCERTAIN':comm.some(c=>c.quality==='STALE')?'STALE':'GOOD';
  return {s,devices,key,quality};
}));
function instanceState(row:(typeof configuredRows.value)[number]){return row.activeAlarmCount?'warning':row.onlineCount<row.devices.length?'offline':'normal';}
function legacyState(row:(typeof legacyRows.value)[number]){return row.s.activeAlarmCount?'warning':row.s.onlineCount<row.s.deviceCount?'offline':'normal';}
function instanceLabel(templateId:string){return scenarioLabels[templateId as ScenarioType]??templateId;}
</script>
<template><section class="operator-page">
  <PageHeader title="安全监测" eyebrow="运行监控" description="查看各类安全监测应用的在线状态、关键数据和报警情况。"/>
  <section class="hmi-panel scenario-matrix-panel">
    <div class="hmi-panel-head"><div><span>安全监测</span><h2>{{configuredRows.length?'监测应用':'监测状态'}}</h2></div><small>{{configuredRows.length||legacyRows.length}} 个对象</small></div>
    <div v-if="configuredRows.length" class="scenario-matrix">
      <div class="scenario-matrix-head"><span>监测应用</span><span>设备</span><span>关键数据</span><span>通信</span><span>报警</span><span></span></div>
      <RouterLink v-for="row in configuredRows" :key="row.instance.instanceId" :to="`/applications/${row.instance.instanceId}`" class="scenario-matrix-row" :data-scenario="row.instance.templateId" :data-state="instanceState(row)">
        <div class="scenario-name"><span class="hmi-state-indicator"><i></i></span><div><strong>{{row.instance.displayName}}</strong><small>{{instanceLabel(row.instance.templateId)}} · {{row.instance.instanceId}}</small></div></div>
        <span>在线 {{row.onlineCount}} / {{row.devices.length}}</span>
        <div class="scenario-key-value"><strong>{{formatTelemetryText(row.key)}}</strong><small>{{row.key?.label??'暂无绑定数据'}}</small></div>
        <span>{{qualityLabel[row.quality]}}</span><b :class="row.activeAlarmCount?'text-warning':''">{{row.activeAlarmCount}}</b><AppIcon name="chevronRight" :size="15"/>
      </RouterLink>
    </div>
    <div v-else class="scenario-matrix">
      <div class="scenario-matrix-head"><span>监测场景</span><span>设备</span><span>关键数据</span><span>通信</span><span>报警</span><span></span></div>
      <RouterLink v-for="row in legacyRows" :key="row.s.type" :to="`/scenarios/${row.s.type}`" class="scenario-matrix-row" :data-scenario="row.s.type" :data-state="legacyState(row)">
        <div class="scenario-name"><span class="hmi-state-indicator"><i></i></span><div><strong>{{row.s.label}}</strong><small>{{scenarioDescriptions[row.s.type]}}</small></div></div><span>在线 {{row.s.onlineCount}} / {{row.s.deviceCount}}</span><div class="scenario-key-value"><strong>{{formatTelemetryText(row.key)}}</strong><small>{{row.key?.label??'暂无数据'}}</small></div><span>{{qualityLabel[row.quality]}}</span><b :class="row.s.activeAlarmCount?'text-warning':''">{{row.s.activeAlarmCount}}</b><AppIcon name="chevronRight" :size="15"/>
      </RouterLink>
    </div>
  </section>
</section></template>
