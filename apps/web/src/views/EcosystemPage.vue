<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import PageHeader from '../components/PageHeader.vue';
import StatusPill from '../components/StatusPill.vue';
import AppIcon from '../components/AppIcon.vue';

const active=ref<'store'|'lowcode'|'api'>('store');
type DemoApp={id:string;name:string;category:string;version:string;summary:string;mounted:boolean;core:boolean};
const apps=ref<DemoApp[]>([
  {id:'safety-suite',name:'五类安全监测套件',category:'安全监测',version:'1.0.0',summary:'温湿度、红外照明、危险气体、AGV 避障、货物计数。',mounted:true,core:true},
  {id:'inspection',name:'巡检管理',category:'生产运营',version:'0.9.0',summary:'巡检计划、执行、异常和闭环。',mounted:false,core:false},
  {id:'predictive',name:'预测性维护',category:'智能分析',version:'0.8.0',summary:'设备健康、异常识别和维护建议。',mounted:false,core:false},
  {id:'vision',name:'工业视觉 AI',category:'智能分析',version:'0.8.0',summary:'火焰识别、安全帽检测和视觉任务接入。',mounted:false,core:false},
  {id:'energy-carbon',name:'能源与双碳',category:'设备能源',version:'0.7.0',summary:'能耗、碳排核算和节能建议展示。',mounted:false,core:false}
]);
const palette=['数据源','实时指标','仪表组件','报警规则','状态表格','受控操作','AI 智能体'];
const canvas=ref<string[]>(['数据源','实时指标','仪表组件']);
const dragItem=ref('');
const showRegistration=ref(false);
const draftName=ref('');
const draftCategory=ref('第三方应用');
const mountedCount=computed(()=>apps.value.filter(item=>item.mounted).length);
function toggle(app:DemoApp){if(app.core)return;app.mounted=!app.mounted;}
function dragStart(name:string){dragItem.value=name;}
function drop(){if(!dragItem.value)return;canvas.value=[...canvas.value,dragItem.value];dragItem.value='';}
function remove(index:number){canvas.value=canvas.value.filter((_,i)=>i!==index);}
function exportCanvas(){
  const payload={schema:'smart-factory-app-manifest/v1',name:'低代码应用',createdAt:new Date().toISOString(),components:canvas.value};
  const blob=new Blob([JSON.stringify(payload,null,2)],{type:'application/json;charset=utf-8'});
  const url=URL.createObjectURL(blob);const a=document.createElement('a');a.href=url;a.download='lowcode-app.smartapp.json';a.click();URL.revokeObjectURL(url);
}
function registerThirdParty(){
  const name=draftName.value.trim();if(!name)return;
  apps.value.push({id:`third-${Date.now()}`,name,category:draftCategory.value.trim()||'第三方应用',version:'0.1.0',summary:'第三方应用接入登记（本地演示记录）。',mounted:false,core:false});
  draftName.value='';draftCategory.value='第三方应用';showRegistration.value=false;active.value='store';
}
onMounted(()=>{
  try{const raw=localStorage.getItem('smart-factory.ecosystem.apps');if(raw){const saved=JSON.parse(raw) as Array<{id:string;mounted:boolean}>;for(const row of saved){const app=apps.value.find(i=>i.id===row.id);if(app&&!app.core)app.mounted=Boolean(row.mounted);}}}catch{}
});
watch(apps,(value)=>{try{localStorage.setItem('smart-factory.ecosystem.apps',JSON.stringify(value.map(({id,mounted})=>({id,mounted}))));}catch{}},{deep:true});
const apis=[
  {name:'设备与遥测',scope:'设备状态、实时测点、历史数据',mode:'REST + WebSocket'},
  {name:'报警与事件',scope:'报警状态、确认、事件处置',mode:'REST + WebSocket'},
  {name:'受控命令',scope:'命令预览、权限、确认与反馈',mode:'REST'},
  {name:'平台配置',scope:'资产、应用实例、规则与配置',mode:'REST'},
  {name:'应用接入',scope:'第三方应用通过平台接口读取和挂载能力',mode:'Open API'}
];
</script>

<template>
<section class="operator-page ecosystem-page">
  <PageHeader title="应用生态" eyebrow="应用商店 · 低代码画布 · 开放 API" description="新增业务优先通过应用层扩展，尽量保持底层平台、现场设备、通信和核心运行链稳定。">
    <template #actions><StatusPill tone="info" label="应用层扩展机制"/></template>
  </PageHeader>

  <div class="ecosystem-summary-strip">
    <div><span>核心入口</span><strong>3</strong><small>商店 / 画布 / API</small></div>
    <div><span>应用目录</span><strong>{{apps.length}}</strong><small>演示目录</small></div>
    <div><span>已挂载</span><strong>{{mountedCount}}</strong><small>本地展示状态</small></div>
    <div><span>底层策略</span><strong>稳定</strong><small>业务扩展不改核心</small></div>
  </div>

  <nav class="domain-tabs"><button :class="{'is-active':active==='store'}" @click="active='store'">应用商店</button><button :class="{'is-active':active==='lowcode'}" @click="active='lowcode'">低代码画布</button><button :class="{'is-active':active==='api'}" @click="active='api'">开放 API</button></nav>

  <section v-if="active==='store'" class="app-store-grid">
    <article v-for="app in apps" :key="app.id" class="app-store-card hmi-panel" :data-mounted="app.mounted">
      <div class="app-store-icon"><AppIcon name="system" :size="22"/></div><div class="app-store-title"><span>{{app.category}}</span><h2>{{app.name}}</h2></div><StatusPill :tone="app.mounted?'good':'muted'" :label="app.mounted?'已挂载':'未挂载'"/>
      <p>{{app.summary}}</p><div class="app-store-meta"><span>v{{app.version}}</span><span>独立应用包</span></div>
      <button type="button" :disabled="app.core" :class="app.mounted?'btn-secondary':'btn-primary'" @click="toggle(app)">{{app.core?'核心应用':' '+(app.mounted?'卸载':'一键挂载') }}</button>
    </article>
  </section>

  <section v-else-if="active==='lowcode'" class="lowcode-layout">
    <aside class="lowcode-palette hmi-panel"><div class="hmi-panel-head"><div><span>组件库</span><h2>拖拽组件</h2></div><small>本地演示画布</small></div><button v-for="item in palette" :key="item" draggable="true" @dragstart="dragStart(item)"><AppIcon name="scenarios" :size="16"/>{{item}}</button></aside>
    <div class="lowcode-canvas hmi-panel" @dragover.prevent @drop="drop"><div class="lowcode-canvas-head"><div><span>新应用</span><strong>工程师编排画布</strong></div><StatusPill tone="info" label="演示"/></div><div class="lowcode-flow"><article v-for="(item,index) in canvas" :key="`${item}-${index}`"><span>{{index+1}}</span><strong>{{item}}</strong><button type="button" @click="remove(index)">×</button></article><div v-if="!canvas.length" class="lowcode-empty">将左侧组件拖到此处</div></div><footer><span>画布不写入生产配置；可导出独立应用清单用于演示。</span><button class="btn-primary" type="button" :disabled="!canvas.length" @click="exportCanvas">导出应用包</button></footer></div>
  </section>

  <section v-else class="open-api-panel hmi-panel">
    <div class="open-api-intro"><div><span>OPEN API</span><h2>第三方系统与应用接入</h2></div><p>开放接口继续复用现有设备、遥测、报警、命令和平台配置边界，不额外改写后端核心。实际路径、认证和权限以当前部署配置为准。</p></div>
    <div class="open-api-table"><div class="open-api-row open-api-head"><span>能力域</span><span>范围</span><span>方式</span><span>状态</span></div><div v-for="item in apis" :key="item.name" class="open-api-row"><strong>{{item.name}}</strong><span>{{item.scope}}</span><b>{{item.mode}}</b><StatusPill tone="good" label="平台已有边界"/></div></div>
    <div class="open-api-actions"><button class="btn-secondary" type="button" @click="showRegistration=true">登记第三方应用</button><RouterLink class="btn-secondary" to="/communication">工业通信</RouterLink><RouterLink class="btn-secondary" to="/configuration">平台配置</RouterLink><RouterLink class="btn-primary" to="/system">系统管理</RouterLink></div>
  </section>

  <section class="domain-data-boundary"><div><span>扩展原则</span><strong>应用层扩展，核心平台稳定</strong></div><p>商店挂载状态、低代码画布和未接入应用使用本地演示层；不伪造现场设备控制或真实生产数据。</p></section>

  <div v-if="showRegistration" class="hmi-modal-backdrop" @click.self="showRegistration=false">
    <section class="hmi-modal ecosystem-register-modal">
      <header><div><span>第三方应用接入</span><strong>应用登记</strong></div><button class="modal-close" type="button" @click="showRegistration=false">×</button></header>
      <div class="ecosystem-register-body"><label><span>应用名称</span><input v-model="draftName" placeholder="例如：厂区巡检应用"/></label><label><span>业务分类</span><input v-model="draftCategory" placeholder="第三方应用"/></label><p>此处只创建本地演示登记记录，不提交生产后端；真实上架需通过开放 API、权限与部署审核。</p></div>
      <footer><button class="btn-secondary" type="button" @click="showRegistration=false">取消</button><button class="btn-primary" type="button" :disabled="!draftName.trim()" @click="registerThirdParty">登记并加入商店</button></footer>
    </section>
  </div>
</section>
</template>
