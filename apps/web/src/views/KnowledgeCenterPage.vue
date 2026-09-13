<script setup lang="ts">
import { computed, ref } from 'vue';
import PageHeader from '../components/PageHeader.vue';
import StatusPill from '../components/StatusPill.vue';

const active=ref<'process'|'procedure'|'case'|'rag'>('process');
const query=ref('');
const entries=[
  {type:'process',title:'铜冶炼主要工艺链',summary:'原料与配料、熔炼、PS 转炉吹炼、火法精炼、阳极浇铸以及烟气净化与制酸。',tags:['熔炼','PS转炉','阳极炉','浇铸']},
  {type:'process',title:'危险气体监测',summary:'危险气体监测需要同时关注浓度、数据质量、通信状态、通风联动和报警处置。',tags:['SO₂','CO','安全']},
  {type:'procedure',title:'报警处置规程（演示条目）',summary:'确认报警来源、核对现场状态、执行授权处置、记录结果并关闭事件。',tags:['报警','处置','规程']},
  {type:'procedure',title:'设备点检规程（演示条目）',summary:'按点检计划核对状态、异常、润滑、电气与安全项目，并形成记录。',tags:['设备','点检','规程']},
  {type:'case',title:'AGV 通信质量下降案例（演示条目）',summary:'通信异常时平台展示链路质量，同时车辆本地避障逻辑保持独立安全边界。',tags:['AGV','通信','案例']},
  {type:'case',title:'危险气体预警案例（演示条目）',summary:'气体浓度进入关注区间后，报警、处置和受控通风操作形成可追溯闭环。',tags:['气体','报警','案例']}
] as const;
const visible=computed(()=>entries.filter(item=>active.value==='rag'||item.type===active.value));
const matches=computed(()=>{
  const q=query.value.trim().toLowerCase();
  if(!q)return [];
  return entries.filter(item=>[item.title,item.summary,...item.tags].join(' ').toLowerCase().includes(q));
});
</script>

<template>
<section class="operator-page knowledge-center-page">
  <PageHeader title="知识中心" eyebrow="工艺知识 · 规程 · 案例 · RAG" description="统一组织工艺知识、规程和案例，并提供可替换为企业知识服务的 RAG 检索入口。当前内置内容明确标记为演示知识索引。">
    <template #actions><StatusPill tone="info" label="演示知识索引"/></template>
  </PageHeader>

  <nav class="domain-tabs" aria-label="知识中心分类">
    <button :class="{'is-active':active==='process'}" @click="active='process'">工艺知识库</button>
    <button :class="{'is-active':active==='procedure'}" @click="active='procedure'">规程</button>
    <button :class="{'is-active':active==='case'}" @click="active='case'">案例</button>
    <button :class="{'is-active':active==='rag'}" @click="active='rag'">RAG 检索</button>
  </nav>

  <section v-if="active==='rag'" class="knowledge-rag-panel hmi-panel">
    <div class="knowledge-rag-copy"><span>本地检索演示</span><h2>检索知识库</h2><p>此处只对内置演示条目进行本地检索；接入企业向量库或知识服务后，可保持同一前端入口。</p></div>
    <div class="knowledge-search"><input v-model="query" class="search-field" placeholder="输入：PS 转炉、报警、AGV、点检……"/><RouterLink class="btn-primary" to="/assistant">生产助手</RouterLink></div>
    <div v-if="query&&!matches.length" class="card-empty">未在演示知识索引中找到匹配内容</div>
    <div v-if="matches.length" class="knowledge-result-list"><article v-for="item in matches" :key="item.title"><span>{{item.type==='process'?'工艺':item.type==='procedure'?'规程':'案例'}}</span><strong>{{item.title}}</strong><p>{{item.summary}}</p></article></div>
  </section>

  <div v-else class="knowledge-grid">
    <article v-for="item in visible" :key="item.title" class="knowledge-card hmi-panel"><span>{{active==='process'?'工艺知识':active==='procedure'?'规程':'案例'}}</span><h2>{{item.title}}</h2><p>{{item.summary}}</p><div><b v-for="tag in item.tags" :key="tag">{{tag}}</b></div></article>
  </div>

  <section class="domain-data-boundary"><div><span>知识边界</span><strong>演示内容与生产知识库隔离</strong></div><p>本页不声称已经接入企业文档、向量数据库或现场规程；后续可通过开放 API / 应用挂载接入真实知识服务。</p></section>
</section>
</template>
