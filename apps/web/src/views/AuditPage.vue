<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import type { AuditEvent } from '@smart-factory/domain';
import PageHeader from '../components/PageHeader.vue';
import StatusPill from '../components/StatusPill.vue';
import { useSiteStore } from '../stores/site';
import { auditActionLabel, auditDetailLabel, auditOutcomeLabel, auditResourceLabel, roleDisplay } from '../utils/operator-labels';
const site=useSiteStore();
const events=ref<AuditEvent[]>([]);const loading=ref(false);const error=ref<string|null>(null);const action=ref('');const actor=ref('');
const filtered=computed(()=>events.value.filter(e=>(!action.value||e.action===action.value)&&(!actor.value||[e.actorId,e.actorDisplayName??''].some(v=>v.toLowerCase().includes(actor.value.toLowerCase())))).sort((a,b)=>b.timestamp-a.timestamp));
function tone(outcome:string):'good'|'warn'|'bad'|'muted'{return['SUCCESS','CONFIRMED','ACCEPTED'].includes(outcome)?'good':['DENIED','REJECTED','FAILED','CONFLICT'].includes(outcome)?'bad':['IDEMPOTENT_REPLAY','NOOP'].includes(outcome)?'warn':'muted';}
async function load(){loading.value=true;error.value=null;try{const r=await site.queryAuditHistory({from:Date.now()-24*3600_000,to:Date.now(),limit:2000});events.value=r.items;if(!r.enabled)error.value='审计存储当前未启用。';}catch(c){error.value=c instanceof Error?c.message:String(c);}finally{loading.value=false;}}
onMounted(load);
</script>
<template><section class="operator-page">
<PageHeader title="操作记录" eyebrow="安全与事件" description="查看控制操作、报警确认和登录等关键操作记录。"><template #actions><button class="btn-secondary" type="button" :disabled="loading" @click="load">刷新</button></template></PageHeader>
<div v-if="!site.can('audit.view')" class="inline-alert" data-state="warning">当前角色没有审计日志查看权限。</div>
<template v-else>
<div class="toolbar hmi-toolbar"><label class="search-field"><span>操作员</span><input v-model="actor" placeholder="姓名或用户标识"/></label><label class="select-field"><span>操作类型</span><select v-model="action"><option value="">全部</option><option value="AUTH_LOGIN">用户登录</option><option value="COMMAND_REQUEST">设备操作请求</option><option value="COMMAND_RESULT">设备操作结果</option><option value="ALARM_ACK">报警确认</option><option value="ALARM_CLEAR_ATTEMPT">报警恢复处理</option></select></label><span class="toolbar-count">{{filtered.length}} 条记录</span></div>
<div v-if="error" class="inline-alert" data-state="warning">{{error}}</div>
<section class="hmi-panel"><div class="table-card"><table><thead><tr><th>时间</th><th>操作员</th><th>角色</th><th>动作</th><th>对象</th><th>结果</th><th>详情</th></tr></thead><tbody>
<tr v-for="e in filtered" :key="e.eventId"><td class="table-time">{{new Date(e.timestamp).toLocaleString('zh-CN',{hour12:false})}}</td><td><strong>{{e.actorDisplayName||e.actorId}}</strong><small class="table-sub">{{e.actorId}}</small></td><td>{{roleDisplay(e.actorRole)}}</td><td>{{auditActionLabel(e.action)}}</td><td>{{auditResourceLabel(e.resourceType)}}<small class="table-sub">{{e.resourceId}}</small></td><td><StatusPill :tone="tone(e.outcome)" :label="auditOutcomeLabel(e.outcome)"/></td><td>{{auditDetailLabel(e)}}</td></tr>
<tr v-if="!loading&&!filtered.length"><td colspan="7"><div class="table-empty">暂无操作记录</div></td></tr></tbody></table></div></section>
</template></section></template>
