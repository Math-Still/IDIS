<script setup lang="ts">
import { computed, onMounted } from 'vue';
import { qualityLabel } from '@smart-factory/domain';
import { useSiteStore } from '../stores/site';
import PageHeader from '../components/PageHeader.vue';
import StatusPill from '../components/StatusPill.vue';

const store=useSiteStore();
onMounted(()=>store.refreshPlatformConfig().catch(()=>{}));
const activeCritical=computed(()=>store.alarmOccurrencesV2.filter(a=>a.conditionState==='ACTIVE'&&a.severity==='CRITICAL'));
const unacked=computed(()=>store.alarmOccurrencesV2.filter(a=>a.ackState==='UNACKNOWLEDGED'));
const recoveredUnacked=computed(()=>store.alarmOccurrencesV2.filter(a=>a.conditionState==='NORMAL'&&a.ackState==='UNACKNOWLEDGED'));
const alarmTasks=computed(()=>Array.from(new Map([...activeCritical.value,...unacked.value].map(a=>[a.occurrenceId,a])).values()));
const openIncidents=computed(()=>store.incidentsV2.filter(i=>i.status!=='CLOSED'));
const unknownCommands=computed(()=>store.dashboard.commands.filter(c=>c.state==='OUTCOME_UNKNOWN'));
const qualityIssues=computed(()=>store.applicationInstances.flatMap(instance=>Object.entries(instance.bindings).flatMap(([role,ref])=>{const point=store.dashboard.telemetry.find(p=>p.deviceId===ref.deviceId&&p.pointId===ref.pointId);return !point||point.quality!=='GOOD'?[{instance,role,point,ref}]:[];})));
const pendingCount=computed(()=>alarmTasks.value.length+openIncidents.value.length+unknownCommands.value.length+qualityIssues.value.length);
const roleLabels:Record<string,string>={ambientTemperature:'温度',ambientHumidity:'湿度',fanState:'通风状态',occupied:'人员感应',lightState:'照明状态',lightMode:'控制模式',gasConcentration:'气体浓度',obstacleDistance:'障碍距离',motionState:'车辆状态',speed:'运行速度',totalCount:'累计计数',countRate:'当前速率'};
const incidentLabels:Record<string,string>={OPEN:'待处理',IN_PROGRESS:'处理中',RESOLVED:'已处置',CLOSED:'已关闭'};
const domains=[
  {to:'/safety',label:'安全监测',desc:'五类感知、人员安全、报警联动'},
  {to:'/operations',label:'生产运营',desc:'产量、报表、统计、巡检'},
  {to:'/asset-energy',label:'设备与能源',desc:'设备、工单、能耗、双碳'},
  {to:'/intelligence',label:'智能分析',desc:'智能体、预测维护、视觉 AI'},
  {to:'/control',label:'可视化与控制',desc:'数字大屏、远程控制、联动'},
  {to:'/knowledge',label:'知识中心',desc:'工艺、规程、案例、RAG'},
  {to:'/ecosystem',label:'应用生态',desc:'应用商店、低代码、开放 API'}
];
function deviceName(id:string){return store.dashboard.devices.find(d=>d.id===id)?.name??id;}
</script>
<template>
<section class="operator-page">
  <PageHeader title="综合总览" eyebrow="智慧工厂数字化平台" description="集中查看全厂运行状态，并进入安全、生产、设备能源、智能分析、可视化控制、知识与应用生态。" />
  <div class="business-domain-launcher"><RouterLink v-for="item in domains" :key="item.to" :to="item.to"><strong>{{item.label}}</strong><span>{{item.desc}}</span><b>进入 →</b></RouterLink></div>
  <div class="overview-kpi-grid">
    <div class="hmi-panel"><span>待处理总数</span><strong>{{pendingCount}}</strong><small>报警、处置、待确认操作和数据状态</small></div>
    <div class="hmi-panel"><span>严重活动报警</span><strong>{{activeCritical.length}}</strong><small>当前条件仍异常</small></div>
    <div class="hmi-panel"><span>未确认报警</span><strong>{{unacked.length}}</strong><small>其中已恢复未确认 {{recoveredUnacked.length}}</small></div>
    <div class="hmi-panel"><span>待确认操作</span><strong>{{unknownCommands.length}}</strong><small>结果不明的操作不会自动重发</small></div>
  </div>

  <div class="overview-monitoring-grid">
    <section class="hmi-panel">
      <div class="hmi-panel-head"><div><span>报警与处置</span><h2>现在需要处理</h2></div><RouterLink to="/incidents">全部处置</RouterLink></div>
      <div v-if="alarmTasks.length||openIncidents.length" class="workbench-task-list">
        <RouterLink v-for="a in alarmTasks.slice(0,6)" :key="a.occurrenceId" to="/incidents" class="workbench-task-row">
          <StatusPill :tone="a.severity==='CRITICAL'?'bad':'warn'" :label="a.conditionState==='NORMAL'?'已恢复待确认':'报警待处理'"/><div><strong>{{a.title}}</strong><small>{{deviceName(a.deviceId)}} · {{new Date(a.raisedAt).toLocaleTimeString('zh-CN',{hour12:false})}}</small></div>
        </RouterLink>
        <RouterLink v-for="i in openIncidents.slice(0,4)" :key="i.incidentId" :to="`/incidents/${i.incidentId}`" class="workbench-task-row"><StatusPill tone="info" :label="incidentLabels[i.status]??i.status"/><div><strong>{{i.title}}</strong><small>{{i.assignee||'未分配'}} · {{new Date(i.updatedAt).toLocaleTimeString('zh-CN',{hour12:false})}}</small></div></RouterLink>
      </div>
      <div v-else class="card-empty">当前没有待处理报警或处置事件</div>
    </section>

    <section class="hmi-panel">
      <div class="hmi-panel-head"><div><span>操作状态</span><h2>待确认操作</h2></div><RouterLink to="/audit" v-if="store.can('audit.view')">操作记录</RouterLink></div>
      <div v-if="unknownCommands.length" class="workbench-task-list"><RouterLink v-for="c in unknownCommands.slice(0,6)" :key="c.id" :to="`/devices/${c.deviceId}`" class="workbench-task-row"><StatusPill tone="warn" label="结果不明"/><div><strong>{{c.action}}</strong><small>{{deviceName(c.deviceId)}} · {{new Date(c.updatedAt).toLocaleTimeString('zh-CN',{hour12:false})}}</small></div></RouterLink></div>
      <div v-else class="card-empty">当前没有结果待确认的操作</div>
    </section>

    <section class="hmi-panel">
      <div class="hmi-panel-head"><div><span>数据状态</span><h2>监测数据质量</h2></div><RouterLink to="/applications">应用监测</RouterLink></div>
      <div v-if="qualityIssues.length" class="workbench-task-list"><RouterLink v-for="q in qualityIssues.slice(0,8)" :key="`${q.instance.instanceId}:${q.role}`" :to="`/applications/${q.instance.instanceId}`" class="workbench-task-row"><StatusPill tone="warn" :label="q.point?qualityLabel[q.point.quality]:'无数据'"/><div><strong>{{q.instance.displayName}}</strong><small>{{roleLabels[q.role]??q.role}} · {{deviceName(q.ref.deviceId)}}</small></div></RouterLink></div>
      <div v-else class="card-empty">所有监测应用数据当前正常</div>
    </section>
  </div>
</section>
</template>
