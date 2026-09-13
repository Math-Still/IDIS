<script setup lang="ts">
import { computed, ref } from 'vue';
import { deviceInterface, deviceProtocol, deviceStatusLabel, qualityLabel } from '@smart-factory/domain';
import { useSiteStore } from '../stores/site';
import PageHeader from '../components/PageHeader.vue';
import StatusPill from '../components/StatusPill.vue';
import AppIcon from '../components/AppIcon.vue';
import { formatDurationSeconds } from '../utils/industrial-format';
const store=useSiteStore();
const query=ref(''); const status=ref('ALL');
const filtered=computed(()=>store.dashboard.devices.filter((d)=>{
  const q=query.value.trim().toLowerCase();
  const matches=!q || [d.name,d.location,deviceProtocol(d),deviceInterface(d),d.id].some(v=>v.toLowerCase().includes(q));
  return matches && (status.value==='ALL'||d.status===status.value);
}));
function tone(s:string):'good'|'warn'|'info'|'muted'{return s==='ONLINE'?'good':s==='DEGRADED'?'warn':s==='MAINTENANCE'?'info':'muted';}
</script>
<template><section class="operator-page">
  <PageHeader title="设备与感知" eyebrow="设备与生产" description="查看现场设备、位置、运行状态、通信接口和最近数据时间。"/>
  <div class="toolbar hmi-toolbar">
    <label class="search-field"><AppIcon name="search" :size="15"/><input v-model="query" type="search" placeholder="设备名称 / 位置 / 协议 / 编号"/></label>
    <label class="select-field"><span>状态</span><select v-model="status"><option value="ALL">全部</option><option value="ONLINE">在线</option><option value="DEGRADED">降级</option><option value="MAINTENANCE">维护</option><option value="OFFLINE">离线</option></select></label>
    <span class="toolbar-count">{{filtered.length}} / {{store.dashboard.devices.length}} 台设备</span>
  </div>
  <section class="hmi-panel device-table-panel">
    <div class="device-hmi-table">
      <div class="device-hmi-head"><span>设备</span><span>状态</span><span>位置</span><span>接口 / 协议</span><span>数据质量</span><span>最后数据</span><span></span></div>
      <RouterLink v-for="d in filtered" :key="d.id" :to="`/devices/${d.id}`" class="device-hmi-row" :data-scenario="d.scenario" :data-state="d.status.toLowerCase()">
        <div><strong>{{d.name}}</strong><small>{{d.id}}</small></div>
        <StatusPill :tone="tone(d.status)" :label="deviceStatusLabel[d.status]"/>
        <span>{{d.location}}</span>
        <span>{{deviceInterface(d)}} · {{deviceProtocol(d)}}</span>
        <span>{{qualityLabel[store.dashboard.communication.find(c=>c.deviceId===d.id)?.quality ?? 'STALE']}}</span>
        <time>{{formatDurationSeconds(d.lastSeen)}}</time>
        <AppIcon name="chevronRight" :size="14"/>
      </RouterLink>
      <div v-if="!filtered.length" class="table-empty">当前筛选条件下无设备</div>
    </div>
  </section>
</section></template>
