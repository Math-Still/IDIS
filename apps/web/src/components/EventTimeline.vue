<script setup lang="ts">
import { onMounted, ref, watch } from 'vue';
import type { TimelineItemV2 } from '@smart-factory/domain';
import { useSiteStore } from '../stores/site';
import StatusPill from './StatusPill.vue';

const props=withDefaults(defineProps<{instanceId?:string;from?:number;to?:number;limit?:number;items?:TimelineItemV2[]}>(),{limit:80});
const store=useSiteStore();
const rows=ref<TimelineItemV2[]>([]); const loading=ref(false); const error=ref('');
const domainLabels:Record<string,string>={ALARM:'报警',COMMAND:'操作',INCIDENT:'处置',CONFIG:'配置'};
const eventLabels:Record<string,string>={RAISED:'报警产生',ACKNOWLEDGED:'报警确认',CLEARED:'报警恢复',OPEN:'开始记录',START:'开始处置',RESOLVE:'完成处置',CLOSE:'关闭事件',REOPEN:'重新打开',ISSUED:'操作提交',APPLIED:'设备已接收',CONFIRMED:'设备已确认',PUBLISHED:'配置更新'};
const stateLabels:Record<string,string>={ACTIVE:'异常持续',NORMAL:'已恢复',UNKNOWN:'状态不明',UNACKNOWLEDGED:'待确认',ACKNOWLEDGED:'已确认',OPEN:'待处理',IN_PROGRESS:'处理中',RESOLVED:'已处置',CLOSED:'已关闭',OUTCOME_UNKNOWN:'结果待确认',CONFIRMED:'已确认',FAILED:'失败',REJECTED:'已拒绝'};
function domainTone(domain:string):'info'|'warn'|'good'|'muted'{return domain==='ALARM'?'warn':domain==='COMMAND'?'info':domain==='INCIDENT'?'good':'muted';}
function label(item:TimelineItemV2):string{return `${domainLabels[item.timelineDomain]??item.timelineDomain} · ${eventLabels[item.eventType]??item.eventType}`;}
function stateText(item:TimelineItemV2):string{const value=String(item.state??item.conditionState??item.status??item.conclusion??'记录');return stateLabels[value]??value;}
async function load(){
  if(props.items){rows.value=[...props.items].slice(-props.limit).reverse();loading.value=false;error.value='';return;}
  loading.value=true;error.value='';try{const to=props.to??Date.now(),from=props.from??to-60*60*1000;const result=await store.queryTimelineV2({from,to,instanceId:props.instanceId});rows.value=result.items.slice(-props.limit).reverse();}catch(e){error.value=e instanceof Error?e.message:String(e);}finally{loading.value=false;}
}
onMounted(load);watch(()=>[props.instanceId,props.from,props.to,props.items],load,{deep:true});
</script>
<template>
  <div class="event-timeline">
    <div v-if="loading" class="card-empty">正在读取时间线…</div>
    <div v-else-if="error" class="card-empty">{{error}}</div>
    <div v-else-if="!rows.length" class="card-empty">所选时间范围暂无关联事件</div>
    <article v-for="item in rows" :key="item.eventId" class="event-timeline-row">
      <time>{{new Date(item.timestamp).toLocaleString('zh-CN',{hour12:false})}}</time>
      <StatusPill :tone="domainTone(item.timelineDomain)" :label="label(item)" />
      <div><strong>{{stateText(item)}}</strong><small>{{item.actor ? `操作人 ${item.actor}` : item.deviceId ? `设备 ${item.deviceId}` : item.comment ?? ''}}</small></div>
    </article>
  </div>
</template>
