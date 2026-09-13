<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { severityLabel, type AlarmOccurrenceV2, type IncidentV2, type TimelineItemV2 } from '@smart-factory/domain';
import { useSiteStore } from '../stores/site';
import PageHeader from '../components/PageHeader.vue';
import StatusPill from '../components/StatusPill.vue';
import EventTimeline from '../components/EventTimeline.vue';

const store=useSiteStore(); const route=useRoute(); const router=useRouter();
const selectedOccurrence=ref<string|null>(null); const comment=ref(''); const busy=ref(false); const error=ref<string|null>(null); const timeline=ref<TimelineItemV2[]>([]);
const incident=computed<IncidentV2|null>(()=>store.incidentsV2.find((x)=>x.incidentId===route.params.id)??null);
const unhandled=computed(()=>store.alarmOccurrencesV2.filter((o)=>o.conditionState!=='NORMAL'||o.ackState!=='ACKNOWLEDGED'));
const incidentLabels:Record<string,string>={OPEN:'待处理',IN_PROGRESS:'处理中',RESOLVED:'已处置',CLOSED:'已关闭'};
const conditionLabels:Record<string,string>={ACTIVE:'异常持续',NORMAL:'已恢复',UNKNOWN:'状态不明'};
const ackLabels:Record<string,string>={UNACKNOWLEDGED:'待确认',ACKNOWLEDGED:'已确认'};
function tone(o:AlarmOccurrenceV2):'info'|'warn'|'bad'{return o.severity==='CRITICAL'?'bad':o.severity==='WARNING'?'warn':'info';}
function deviceName(id:string){return store.dashboard.devices.find(d=>d.id===id)?.name??id;}
async function refresh(){await store.refreshB3Operations();const result=await store.queryTimelineV2({from:Date.now()-24*3600_000,to:Date.now()});timeline.value=result.items.filter(e=>!incident.value||e.incidentId===incident.value.incidentId||incident.value.occurrenceIds.includes(e.occurrenceId??''));}
async function createIncident(){if(!selectedOccurrence.value)return;busy.value=true;error.value=null;try{const source=store.alarmOccurrencesV2.find(o=>o.occurrenceId===selectedOccurrence.value);const created=await store.createIncidentV2({occurrenceIds:[selectedOccurrence.value],title:source?`${source.title}处置`:'报警处置'});await router.push(`/incidents/${created.incidentId}`);await refresh();}catch(c){error.value=c instanceof Error?c.message:String(c);}finally{busy.value=false;}}
async function ack(id:string){busy.value=true;error.value=null;try{await store.acknowledgeAlarmOccurrenceV2(id,comment.value.trim()||undefined);comment.value='';await refresh();}catch(c){error.value=c instanceof Error?c.message:String(c);}finally{busy.value=false;}}
async function act(action:'START'|'RESOLVE'|'CLOSE'|'REOPEN'){if(!incident.value)return;busy.value=true;error.value=null;try{await store.transitionIncidentV2(incident.value.incidentId,{action,expectedRevision:incident.value.revision,comment:comment.value.trim()||undefined});comment.value='';await refresh();}catch(c){error.value=c instanceof Error?c.message:String(c);}finally{busy.value=false;}}
onMounted(()=>void refresh());
</script>

<template><section class="operator-page">
  <PageHeader title="报警与处置" eyebrow="当班处置" description="查看报警状态、确认情况、责任人和处置记录。"/>
  <div v-if="error" class="inline-alert" data-state="bad">{{error}}</div>

  <section v-if="!incident" class="hmi-panel">
    <div class="hmi-panel-head"><div><span>当前报警</span><h2>待处理事项</h2></div><small>{{unhandled.length}} 项</small></div>
    <div class="table-card"><table><thead><tr><th>时间</th><th>设备</th><th>报警</th><th>状态</th><th>确认</th><th>操作</th></tr></thead><tbody>
      <tr v-for="o in unhandled" :key="o.occurrenceId"><td>{{new Date(o.raisedAt).toLocaleString('zh-CN',{hour12:false})}}</td><td>{{deviceName(o.deviceId)}}</td><td><StatusPill :tone="tone(o)" :label="severityLabel[o.severity]"/> {{o.title}}</td><td>{{conditionLabels[o.conditionState]??o.conditionState}}</td><td>{{ackLabels[o.ackState]??o.ackState}}</td><td><button v-if="o.ackState==='UNACKNOWLEDGED'" class="btn-secondary" type="button" :disabled="busy||!store.can('alarm.ack')" @click="ack(o.occurrenceId)">确认</button> <button class="btn-primary" type="button" :disabled="busy||!store.can('incident.assign')" @click="selectedOccurrence=o.occurrenceId;createIncident()">创建处置</button></td></tr>
      <tr v-if="!unhandled.length"><td colspan="6" class="table-empty">当前没有待处理报警</td></tr>
    </tbody></table></div>
    <div class="hmi-panel-head"><div><span>当班记录</span><h2>处置事项</h2></div><small>{{store.incidentsV2.length}} 项</small></div>
    <div class="table-card"><table><thead><tr><th>事项</th><th>状态</th><th>责任人</th><th>更新时间</th></tr></thead><tbody><tr v-for="i in store.incidentsV2" :key="i.incidentId" @click="router.push(`/incidents/${i.incidentId}`)"><td>{{i.title}}</td><td>{{incidentLabels[i.status]??i.status}}</td><td>{{i.assignee||'未分配'}}</td><td>{{new Date(i.updatedAt).toLocaleString('zh-CN',{hour12:false})}}</td></tr><tr v-if="!store.incidentsV2.length"><td colspan="4" class="table-empty">暂无处置事项</td></tr></tbody></table></div>
  </section>

  <template v-else>
    <section class="hmi-panel"><div class="hmi-panel-head"><div><span>处置详情</span><h2>{{incident.title}}</h2></div><StatusPill tone="info" :label="incidentLabels[incident.status]??incident.status"/></div><label class="form-field"><span>处置备注</span><textarea v-model="comment" rows="2" maxlength="1000" placeholder="填写处置情况"></textarea></label><div class="table-actions"><button class="btn-secondary" @click="router.push('/incidents')">返回列表</button><button v-if="incident.status==='OPEN'" class="btn-primary" :disabled="busy" @click="act('START')">开始处置</button><button v-if="incident.status==='OPEN'||incident.status==='IN_PROGRESS'" class="btn-primary" :disabled="busy" @click="act('RESOLVE')">标记已处置</button><button v-if="incident.status==='RESOLVED'" class="btn-primary" :disabled="busy||!store.can('incident.close')" @click="act('CLOSE')">复核关闭</button><button v-if="incident.status==='CLOSED'" class="btn-secondary" :disabled="busy" @click="act('REOPEN')">重新打开</button></div></section>
    <section class="hmi-panel"><div class="hmi-panel-head"><div><span>运行记录</span><h2>处置时间线</h2></div><small>最近 1 小时</small></div><EventTimeline :items="timeline" :limit="80"/></section>
  </template>
</section></template>
