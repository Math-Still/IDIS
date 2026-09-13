<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue';
import type { Alarm, AlarmLifecycleEvent } from '@smart-factory/domain';
import { severityLabel } from '@smart-factory/domain';
import { alarmLifecycleLabel, alarmStateLabel, lifecycleSourceLabel, roleDisplay } from '../utils/operator-labels';
import { useSiteStore } from '../stores/site';
import PageHeader from '../components/PageHeader.vue';
import StatusPill from '../components/StatusPill.vue';
import AppIcon from '../components/AppIcon.vue';

const store = useSiteStore();
const query = ref('');
const severity = ref('ALL');
const state = ref('ALL');
const page = ref(1);
const pageSize = 20;
const actionTarget = ref<Alarm|null>(null);
const comment = ref('');
const actionBusy = ref(false);
const actionError = ref<string|null>(null);
const historyTarget = ref<Alarm|null>(null);
const historyEvents = ref<AlarmLifecycleEvent[]>([]);
const historyLoading = ref(false);
const historyError = ref<string|null>(null);

const filtered = computed(() => [...store.dashboard.alarms].filter((a) => {
  const q=query.value.trim().toLowerCase();
  const device=store.dashboard.devices.find(d=>d.id===a.deviceId);
  return (!q||[a.title,a.message,a.deviceId,device?.name??''].some(v=>v.toLowerCase().includes(q))) &&
    (severity.value==='ALL'||a.severity===severity.value) && (state.value==='ALL'||a.state===state.value);
}).sort((a,b)=>b.raisedAt-a.raisedAt));
const pageCount=computed(()=>Math.max(1,Math.ceil(filtered.value.length/pageSize)));
const paged=computed(()=>{if(page.value>pageCount.value)page.value=pageCount.value;const start=(page.value-1)*pageSize;return filtered.value.slice(start,start+pageSize);});
const counts=computed(()=>({critical:store.dashboard.alarms.filter(a=>a.severity==='CRITICAL'&&a.state!=='CLEARED').length,warning:store.dashboard.alarms.filter(a=>a.severity==='WARNING'&&a.state!=='CLEARED').length,active:store.dashboard.alarms.filter(a=>a.state==='ACTIVE').length,ack:store.dashboard.alarms.filter(a=>a.state==='ACKNOWLEDGED').length}));
function tone(s:string):'info'|'warn'|'bad'{return s==='CRITICAL'?'bad':s==='WARNING'?'warn':'info';}
function openAck(alarm:Alarm){ actionTarget.value=alarm; comment.value=''; actionError.value=null; }
function closeAck(){ if(!actionBusy.value){ actionTarget.value=null; actionError.value=null; } }
async function submitAck(){
  if(!actionTarget.value) return;
  actionError.value=null;
  actionBusy.value=true;
  try{
    await store.acknowledgeAlarm(actionTarget.value.id,{comment:comment.value.trim()||undefined});
    actionTarget.value=null;
  }catch(cause){ actionError.value=cause instanceof Error?cause.message:String(cause); }
  finally{ actionBusy.value=false; }
}
function handleKeydown(event:KeyboardEvent){ if(event.key==='Escape'){ if(actionTarget.value&&!actionBusy.value) closeAck(); historyTarget.value=null; } }
onMounted(()=>window.addEventListener('keydown',handleKeydown));
onUnmounted(()=>window.removeEventListener('keydown',handleKeydown));
async function openHistory(alarm:Alarm){
  historyTarget.value=alarm; historyEvents.value=[]; historyError.value=null; historyLoading.value=true;
  try{
    const response=await store.queryAlarmHistory({alarmId:alarm.id,from:Math.max(0,alarm.raisedAt-60_000),to:Date.now(),limit:500});
    historyEvents.value=response.items;
    if(!response.enabled) historyError.value='当前没有可查询的报警记录。';
  }catch(cause){ historyError.value=cause instanceof Error?cause.message:String(cause); }
  finally{ historyLoading.value=false; }
}
</script>

<template><section class="operator-page">
  <PageHeader title="报警事件" eyebrow="安全与事件"/>
  <div class="alarm-summary-strip"><div data-severity="critical"><span>严重报警</span><strong>{{counts.critical}}</strong></div><div data-severity="warning"><span>警告</span><strong>{{counts.warning}}</strong></div><div><span>未确认</span><strong>{{counts.active}}</strong></div><div><span>已确认</span><strong>{{counts.ack}}</strong></div></div>
  <div class="toolbar hmi-toolbar"><label class="search-field"><AppIcon name="search" :size="15"/><input v-model="query" type="search" placeholder="告警标题 / 设备 / 描述"/></label><label class="select-field"><span>等级</span><select v-model="severity"><option value="ALL">全部</option><option value="CRITICAL">严重</option><option value="WARNING">警告</option><option value="INFO">提示</option></select></label><label class="select-field"><span>状态</span><select v-model="state"><option value="ALL">全部</option><option value="ACTIVE">未确认</option><option value="ACKNOWLEDGED">已确认</option><option value="CLEARED">已恢复</option></select></label><span class="toolbar-count">{{filtered.length}} 条记录</span></div>

  <section class="hmi-panel alarm-table-panel"><div class="table-card"><table class="alarm-table"><thead><tr><th>产生时间</th><th>等级</th><th>对象</th><th>报警描述</th><th>生命周期</th><th>操作</th></tr></thead><tbody>
    <tr v-for="a in paged" :key="a.id" :data-severity="a.severity.toLowerCase()">
      <td class="table-time">{{new Date(a.raisedAt).toLocaleString('zh-CN',{hour12:false})}}</td>
      <td><StatusPill :tone="tone(a.severity)" :label="severityLabel[a.severity]"/></td>
      <td><strong class="table-primary-cell">{{store.dashboard.devices.find(d=>d.id===a.deviceId)?.name??a.deviceId}}</strong><small class="table-sub">{{a.deviceId}}</small></td>
      <td><strong class="table-primary-cell">{{a.title}}</strong><small class="table-sub">{{a.message}}</small></td>
      <td><span class="state-text" :data-state="a.state.toLowerCase()">{{alarmStateLabel[a.state]}}</span><small v-if="a.acknowledgedAt" class="table-sub">确认于 {{new Date(a.acknowledgedAt).toLocaleString('zh-CN',{hour12:false})}} · {{a.acknowledgedBy}}</small><small v-if="a.clearedAt" class="table-sub">恢复于 {{new Date(a.clearedAt).toLocaleString('zh-CN',{hour12:false})}}</small></td>
      <td><div class="table-actions"><button v-if="a.state==='ACTIVE'" class="btn-primary" :disabled="!store.can('alarm.ack')" type="button" @click="openAck(a)">确认</button><button class="btn-secondary" type="button" @click="openHistory(a)">历史</button><RouterLink class="btn-secondary link-button" :to="{path:'/history',query:{deviceId:a.deviceId,from:String(Math.max(0,a.raisedAt-15*60_000)),to:String((a.clearedAt??Date.now())+15*60_000)}}">趋势</RouterLink></div></td>
    </tr>
    <tr v-if="!filtered.length"><td colspan="6"><div class="table-empty">当前筛选条件下无报警</div></td></tr>
  </tbody></table><div class="table-footer"><span>{{filtered.length}} 条记录</span><div class="pager"><button class="btn-secondary" :disabled="page<=1" @click="page--">上一页</button><span>{{page}} / {{pageCount}}</span><button class="btn-secondary" :disabled="page>=pageCount" @click="page++">下一页</button></div></div></div></section>

  <div v-if="actionTarget" class="hmi-modal-backdrop" @click.self="closeAck"><section class="hmi-modal" role="dialog" aria-modal="true" aria-label="报警确认"><header><div><span>报警确认</span><strong>确认报警</strong></div><button class="modal-close" type="button" :disabled="actionBusy" @click="closeAck">×</button></header><div class="alarm-action-summary" :data-severity="actionTarget.severity.toLowerCase()"><StatusPill :tone="tone(actionTarget.severity)" :label="severityLabel[actionTarget.severity]"/><strong>{{actionTarget.title}}</strong><span>{{actionTarget.message}}</span></div><div class="identity-confirm"><span>确认身份</span><strong>{{store.currentUser?.displayName||'当前操作员'}}</strong><small>{{store.currentUser?.id}} · {{roleDisplay(store.currentUser?.role)}}</small></div><label class="form-field"><span>确认备注</span><textarea v-model="comment" rows="3" maxlength="1000" placeholder="填写处置情况或确认备注"></textarea></label><div v-if="actionError" class="inline-alert" data-state="bad">{{actionError}}</div><footer><button class="btn-secondary" type="button" :disabled="actionBusy" @click="closeAck">取消</button><button class="btn-primary" type="button" :disabled="actionBusy" @click="submitAck">{{actionBusy?'确认中':'确认报警'}}</button></footer></section></div>

  <div v-if="historyTarget" class="hmi-modal-backdrop" @click.self="historyTarget=null"><section class="hmi-modal hmi-modal-wide" role="dialog" aria-modal="true" aria-label="报警生命周期历史"><header><div><span>报警过程</span><strong>{{historyTarget.title}}</strong></div><button class="modal-close" type="button" @click="historyTarget=null">×</button></header><div v-if="historyLoading" class="table-empty">正在读取报警历史</div><div v-else-if="historyError" class="inline-alert" data-state="warning">{{historyError}}</div><ol v-else class="alarm-lifecycle-list"><li v-for="event in historyEvents" :key="event.eventId" :data-event="event.eventType.toLowerCase()"><time>{{new Date(event.timestamp).toLocaleString('zh-CN',{hour12:false})}}</time><strong>{{alarmLifecycleLabel[event.eventType]}}</strong><span>{{lifecycleSourceLabel(event.source)}}</span><small v-if="event.operator">操作员 {{event.operator}}</small><p v-if="event.comment">{{event.comment}}</p></li><li v-if="!historyEvents.length" class="empty-lifecycle">暂无报警生命周期记录</li></ol></section></div>
</section></template>

