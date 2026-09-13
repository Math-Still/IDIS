<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import { useRoute } from 'vue-router';
import { useSiteStore } from '../stores/site';
import AppIcon from '../components/AppIcon.vue';
import PageHeader from '../components/PageHeader.vue';
import StatusPill from '../components/StatusPill.vue';
import { capabilityStatusLabel, workspaceDefinitions, type CapabilityItem } from '../product-capabilities';

const route=useRoute();
const store=useSiteStore();
const workspace=computed(()=>workspaceDefinitions[String(route.meta.workspace??'safety')] ?? workspaceDefinitions.safety);
const activeTab=ref(workspace.value.tabs[0]?.id??'');
const selected=ref<CapabilityItem|null>(null);
watch(workspace,()=>{activeTab.value=workspace.value.tabs[0]?.id??'';selected.value=null;});
const tab=computed(()=>workspace.value.tabs.find(item=>item.id===activeTab.value)??workspace.value.tabs[0]);
const liveCount=computed(()=>tab.value?.items.filter(item=>item.status==='LIVE').length??0);
const demoCount=computed(()=>tab.value?.items.filter(item=>item.status==='DEMO').length??0);
const activeAlarms=computed(()=>store.dashboard.alarms.filter(item=>item.state!=='CLEARED').length);
const summary=computed(()=>[
  {label:'在线设备',value:`${store.onlineDevices}/${store.dashboard.devices.length||0}`,note:'现有设备链路'},
  {label:'活动报警',value:String(activeAlarms.value),note:'现有报警链路'},
  {label:'数据来源',value:store.dataOriginLabel,note:store.dataSource==='DEMO'?'演示数据层':'运行数据层'},
  {label:'本页能力',value:`${liveCount.value} 已接入`,note:demoCount.value?`${demoCount.value} 项演示层`:'全部已接入'}
]);
function tone(status:CapabilityItem['status']):'good'|'info'|'muted'{return status==='LIVE'?'good':status==='PLATFORM'?'info':'muted';}
</script>

<template>
<section class="operator-page capability-workspace">
  <PageHeader :title="workspace.title" :eyebrow="workspace.eyebrow" :description="workspace.description">
    <template #actions><StatusPill :tone="store.dataSource==='DEMO'?'info':'good'" :label="store.dataOriginLabel"/></template>
  </PageHeader>

  <div class="domain-summary-strip">
    <div v-for="item in summary" :key="item.label"><span>{{item.label}}</span><strong>{{item.value}}</strong><small>{{item.note}}</small></div>
  </div>

  <nav class="domain-tabs" aria-label="业务分区">
    <button v-for="item in workspace.tabs" :key="item.id" type="button" :class="{'is-active':activeTab===item.id}" @click="activeTab=item.id">{{item.label}}</button>
  </nav>

  <div class="domain-section-intro"><div><span>当前工作区</span><strong>{{tab.label}}</strong></div><p>{{tab.description}}</p></div>

  <div class="capability-grid">
    <article v-for="item in tab.items" :key="item.id" class="capability-card" :data-status="item.status.toLowerCase()">
      <div class="capability-card-head">
        <span class="capability-symbol"><AppIcon :name="item.status==='LIVE'?'monitor':item.status==='PLATFORM'?'system':'overview'" :size="19"/></span>
        <StatusPill :tone="tone(item.status)" :label="capabilityStatusLabel[item.status]"/>
      </div>
      <h2>{{item.title}}</h2>
      <p>{{item.description}}</p>
      <div class="capability-card-foot">
        <RouterLink v-if="item.link" :to="item.link">{{item.action??'进入功能'}} →</RouterLink>
        <button v-else type="button" @click="selected=item">查看能力</button>
      </div>
    </article>
  </div>

  <section class="domain-data-boundary">
    <div><span>数据边界</span><strong>{{store.dataSource==='DEMO'?'当前使用内置演示数据':'当前使用运行数据接口'}}</strong></div>
    <p>演示层能力只展示产品结构和交互，不声明已连接现场硬件；现有设备、告警、通信、命令和权限链继续沿用原工程。</p>
  </section>

  <div v-if="selected" class="hmi-modal-backdrop" @click.self="selected=null">
    <section class="hmi-modal capability-modal">
      <header><div><span>能力说明</span><strong>{{selected.title}}</strong></div><button class="modal-close" type="button" @click="selected=null">×</button></header>
      <div class="capability-modal-body">
        <StatusPill :tone="tone(selected.status)" :label="capabilityStatusLabel[selected.status]"/>
        <p>{{selected.description}}</p>
        <ul v-if="selected.detail?.length"><li v-for="line in selected.detail" :key="line">{{line}}</li></ul>
        <div v-else class="demo-boundary-note">当前为产品演示入口，未接入现场数据时只使用本地演示数据层，不伪造真实控制结果。</div>
      </div>
      <footer><button class="btn-secondary" type="button" @click="selected=null">关闭</button><RouterLink class="btn-primary" to="/ecosystem">通过应用生态扩展</RouterLink></footer>
    </section>
  </div>
</section>
</template>
