<script setup lang="ts">
import { computed, nextTick, onMounted, ref } from 'vue';
import { useSiteStore } from '../stores/site';
import PageHeader from '../components/PageHeader.vue';
import StatusPill from '../components/StatusPill.vue';
import AppIcon from '../components/AppIcon.vue';

const store=useSiteStore();
const input=ref('');const busy=ref(false);const error=ref('');const scrollHost=ref<HTMLElement|null>(null);
const messages=ref<Array<{role:'assistant'|'user';text:string;time:number}>>([
  {role:'assistant',text:'可以查询当前报警、设备状态、通信情况、监测数据、处置记录和未确认操作。',time:Date.now()}
]);
const quick=[
  '当前有哪些需要优先处理的问题？',
  '检查设备和通信状态，给出处理顺序。',
  '汇总当前报警和相关设备。',
  '有哪些操作结果还需要人工确认？'
];
const modelTone=computed<'good'|'info'>(()=>store.agentStatus?.configured?'good':'info');
const modelLabel=computed(()=>store.agentStatus?.configured?`${store.agentStatus.model}`:'本地运行分析');
const critical=computed(()=>store.dashboard.alarms.filter(a=>a.state!=='CLEARED'&&a.severity==='CRITICAL').length);
const warnings=computed(()=>store.dashboard.alarms.filter(a=>a.state!=='CLEARED'&&a.severity==='WARNING').length);
const uncertain=computed(()=>store.dashboard.commands.filter(c=>c.state==='OUTCOME_UNKNOWN').length);
const degraded=computed(()=>store.dashboard.devices.filter(d=>d.status!=='ONLINE').length);
const online=computed(()=>store.dashboard.devices.filter(d=>d.status==='ONLINE').length);

onMounted(()=>{store.refreshAgentStatus().catch(()=>{});});
async function ask(text?:string){
  const message=(text??input.value).trim();if(!message||busy.value)return;
  const history=messages.value.slice(-12).map(m=>({role:m.role,content:m.text}));
  messages.value.push({role:'user',text:message,time:Date.now()});input.value='';busy.value=true;error.value='';
  await nextTick();scrollHost.value?.scrollTo({top:scrollHost.value.scrollHeight,behavior:'smooth'});
  try{const reply=await store.askAgent(message,history);messages.value.push({role:'assistant',text:reply.answer,time:Date.now()});}
  catch(cause){error.value=cause instanceof Error?cause.message:String(cause);}
  finally{busy.value=false;await nextTick();scrollHost.value?.scrollTo({top:scrollHost.value.scrollHeight,behavior:'smooth'});}
}
</script>

<template>
<section class="operator-page agent-page">
  <PageHeader title="生产助手" eyebrow="运行分析" description="查询设备、报警、通信、监测数据、处置记录和操作状态。">
    <template #actions><StatusPill :tone="modelTone" :label="modelLabel"/></template>
  </PageHeader>

  <div class="agent-summary-strip">
    <div><span>在线设备</span><strong>{{online}}</strong><small>/ {{store.dashboard.devices.length}}</small></div>
    <div><span>严重报警</span><strong :class="{'text-danger':critical}">{{critical}}</strong></div>
    <div><span>一般报警</span><strong>{{warnings}}</strong></div>
    <div><span>异常设备</span><strong>{{degraded}}</strong></div>
    <div><span>结果待确认</span><strong>{{uncertain}}</strong></div>
  </div>

  <div class="agent-layout">
    <section class="agent-conversation">
      <div ref="scrollHost" class="agent-messages">
        <article v-for="(m,index) in messages" :key="index" class="agent-message" :data-role="m.role">
          <div class="agent-message-mark"><AppIcon :name="m.role==='assistant'?'trend':'overview'" :size="16"/></div>
          <div><strong>{{m.role==='assistant'?'生产助手':'我'}}</strong><p>{{m.text}}</p><time>{{new Date(m.time).toLocaleTimeString('zh-CN',{hour12:false})}}</time></div>
        </article>
        <article v-if="busy" class="agent-message" data-role="assistant"><div class="agent-message-mark"><AppIcon name="trend" :size="16"/></div><div><strong>生产助手</strong><p class="agent-thinking">正在读取当前运行状态…</p></div></article>
      </div>
      <div v-if="error" class="inline-alert" data-state="bad">{{error}}</div>
      <form class="agent-input" @submit.prevent="ask()"><textarea v-model="input" rows="2" maxlength="4000" placeholder="输入要查询的问题，例如：哪台设备需要先处理？" @keydown.enter.exact.prevent="ask()"></textarea><button class="btn-primary" type="submit" :disabled="busy||!input.trim()">发送</button></form>
    </section>

    <aside class="agent-side">
      <section class="agent-open-section"><div class="section-heading"><span>常用查询</span><h2>快速分析</h2></div><button v-for="q in quick" :key="q" type="button" class="agent-quick" :disabled="busy" @click="ask(q)">{{q}}<AppIcon name="chevronRight" :size="15"/></button></section>
      <section class="agent-open-section"><div class="section-heading"><span>当前运行</span><h2>查询范围</h2></div><div class="agent-scope-grid"><span>设备状态</span><span>实时测点</span><span>报警处置</span><span>通信状态</span><span>操作记录</span><span>历史记录</span><span>监测应用</span><span>配置规则</span></div></section>
      <section class="agent-open-section"><div class="section-heading"><span>运行概况</span><h2>当前状态</h2></div><dl class="agent-state-list"><div><dt>设备在线</dt><dd>{{online}} / {{store.dashboard.devices.length}}</dd></div><div><dt>活动报警</dt><dd>{{critical+warnings}}</dd></div><div><dt>通信异常</dt><dd>{{store.degradedLinks}}</dd></div><div><dt>待确认操作</dt><dd>{{uncertain}}</dd></div></dl></section>
    </aside>
  </div>
</section>
</template>
