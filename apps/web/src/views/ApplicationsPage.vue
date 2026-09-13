<script setup lang="ts">
import { computed, onMounted } from 'vue';
import { scenarioLabels, type ScenarioType } from '@smart-factory/domain';
import { useSiteStore } from '../stores/site';
import PageHeader from '../components/PageHeader.vue';
import SourceQualityBadge from '../components/SourceQualityBadge.vue';
import AppIcon from '../components/AppIcon.vue';

const store=useSiteStore();
onMounted(()=>store.refreshPlatformConfig().catch(()=>{}));
const groups=computed(()=>{const m=new Map<string,typeof store.applicationInstances>();for(const i of store.applicationInstances)m.set(i.templateId,[...(m.get(i.templateId)??[]),i]);return [...m.entries()];});
function primary(instance:(typeof store.applicationInstances)[number]){const ref=Object.values(instance.bindings)[0];return ref?store.dashboard.telemetry.find(p=>p.deviceId===ref.deviceId&&p.pointId===ref.pointId):undefined;}
function assetName(id:string){return store.platformAssets.find(a=>a.id===id)?.name??id;}
function groupOnline(items:(typeof store.applicationInstances)){const ids=new Set(items.flatMap(i=>Object.values(i.bindings).map(r=>r.deviceId)));return [...ids].filter(id=>store.dashboard.devices.find(d=>d.id===id)?.status==='ONLINE').length;}
function groupDevices(items:(typeof store.applicationInstances)){return new Set(items.flatMap(i=>Object.values(i.bindings).map(r=>r.deviceId))).size;}
function iconFor(type:string){return type==='hazardous_gas'?'gas':type==='agv_obstacle'?'agv':type==='goods_counting'?'count':type==='pir_lighting'?'lighting':'environment';}
</script>
<template>
<section class="operator-page applications-page">
  <PageHeader title="监测应用" eyebrow="运行监测" description="按区域查看环境、照明、危险气体、AGV 和货物计数状态。" />

  <section class="application-summary-strip">
    <div><span>监测应用</span><strong>{{store.applicationInstances.length}}</strong><small>项</small></div>
    <div><span>在线设备</span><strong>{{store.onlineDevices}}</strong><small>/ {{store.dashboard.devices.length}}</small></div>
    <div><span>活动报警</span><strong>{{store.activeAlarms}}</strong><small>条</small></div>
    <div><span>通信异常</span><strong>{{store.degradedLinks}}</strong><small>项</small></div>
  </section>

  <section v-for="([template,items]) in groups" :key="template" class="application-group" :data-scenario="template">
    <div class="application-group-head">
      <div class="application-group-title"><AppIcon :name="iconFor(template)" :size="20"/><div><span>监测类别</span><h2>{{scenarioLabels[template as ScenarioType]??template}}</h2></div></div>
      <div class="application-group-state"><strong>{{groupOnline(items)}} / {{groupDevices(items)}}</strong><span>设备在线</span></div>
    </div>
    <div class="application-row-grid">
      <RouterLink v-for="item in items" :key="item.instanceId" :to="`/applications/${item.instanceId}`" class="application-row-card" :data-scenario="item.templateId">
        <div class="application-row-main"><strong>{{item.displayName}}</strong><small>{{assetName(item.assetId)}}</small></div>
        <SourceQualityBadge :point="primary(item)"/>
        <div class="application-row-meta"><span>{{Object.keys(item.bindings).length}} 个监测点</span><b>查看详情 →</b></div>
      </RouterLink>
    </div>
  </section>
  <div v-if="!groups.length" class="card-empty">当前没有可用的监测应用</div>
</section>
</template>
