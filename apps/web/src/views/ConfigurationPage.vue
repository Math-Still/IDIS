<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import type { ConfigValidationResponse, PlatformConfigSnapshot } from '@smart-factory/domain';
import { useSiteStore } from '../stores/site';
import PageHeader from '../components/PageHeader.vue';
import StatusPill from '../components/StatusPill.vue';

const store=useSiteStore(); const editor=ref(''); const reason=ref(''); const rollbackRevision=ref(''); const busy=ref(false); const message=ref(''); const validation=ref<ConfigValidationResponse|null>(null); const draftId=ref('');
const statusTone=computed<'good'|'warn'>(()=>store.platformConfigStatus?.state==='RUNNING'?'good':'warn');
async function loadSnapshot(){busy.value=true;message.value='';try{await store.refreshPlatformConfig();const snapshot=await store.getPlatformConfigSnapshot();editor.value=JSON.stringify(snapshot,null,2);rollbackRevision.value=store.platformConfigStatus?.runningRevision??'';}catch(error){message.value=error instanceof Error?error.message:String(error);}finally{busy.value=false;}}
async function validate(){busy.value=true;message.value='';validation.value=null;draftId.value='';try{const snapshot=JSON.parse(editor.value) as PlatformConfigSnapshot;const result=await store.validatePlatformConfig(snapshot);validation.value=result.validation;draftId.value=result.draftId;message.value=result.validation.valid?'配置检查通过。':'配置中存在需要修正的内容。';}catch(error){message.value=error instanceof Error?error.message:String(error);}finally{busy.value=false;}}
async function publish(){if(!draftId.value){message.value='请先检查配置。';return;}if(!reason.value.trim()){message.value='请填写变更原因。';return;}busy.value=true;message.value='';try{const result=await store.publishPlatformDraft(draftId.value,reason.value);message.value=result.requiresRestart?'配置已保存，重启服务后启用设备连接变更。':'配置已保存并启用。';draftId.value='';await loadSnapshot();}catch(error){message.value=error instanceof Error?error.message:String(error);}finally{busy.value=false;}}
async function rollback(){if(!rollbackRevision.value.trim()||!reason.value.trim()){message.value='请填写目标版本和变更原因。';return;}busy.value=true;message.value='';try{const result=await store.rollbackPlatformConfig(rollbackRevision.value.trim(),reason.value);message.value=result.requiresRestart?'历史配置已恢复，重启服务后启用设备连接变更。':'历史配置已恢复并启用。';await loadSnapshot();}catch(error){message.value=error instanceof Error?error.message:String(error);}finally{busy.value=false;}}
onMounted(loadSnapshot);
</script>
<template><section class="operator-page configuration-page">
  <PageHeader title="设备与应用配置" eyebrow="系统设置" description="维护区域、设备、监测应用、规则和操作权限。"/>
  <section class="configuration-summary">
    <div><span>当前版本</span><strong>{{store.platformConfigStatus?.runningRevision??'—'}}</strong><StatusPill :tone="statusTone" :label="store.platformConfigStatus?.state==='RUNNING'?'运行中':'待处理'"/></div>
    <div><span>监测应用</span><strong>{{store.applicationInstances.length}}</strong><small>项</small></div>
    <div><span>区域与设备</span><strong>{{store.platformAssets.length}}</strong><small>项</small></div>
  </section>
  <section class="hmi-panel">
    <div class="hmi-panel-head"><div><span>配置内容</span><h2>系统设置</h2></div><button class="btn-secondary" type="button" :disabled="busy" @click="loadSnapshot">重新载入</button></div>
    <label class="form-field"><span>配置内容</span><textarea v-model="editor" rows="24" spellcheck="false" class="config-json-editor"></textarea></label>
    <label class="form-field"><span>变更原因</span><input v-model="reason" maxlength="500" placeholder="填写本次变更原因"/></label>
    <div class="configuration-actions"><button class="btn-secondary" type="button" :disabled="busy||!store.can('config.edit')" @click="validate">检查配置</button><button class="btn-primary" type="button" :disabled="busy||!draftId||!store.can('config.publish')" @click="publish">保存并启用</button></div>
    <div v-if="validation" class="inline-alert" :data-state="validation.valid?'good':'bad'"><strong>{{validation.valid?'配置可用':'配置需要调整'}}</strong><span>{{validation.requiresRestart?'部分设备设置需要重启服务后启用':'可直接启用'}}</span><ul v-if="validation.errors.length"><li v-for="item in validation.errors" :key="`${item.code}:${item.objectId??''}`">{{item.message}}</li></ul></div>
  </section>
  <section class="hmi-panel"><div class="hmi-panel-head"><div><span>历史配置</span><h2>恢复以前的设置</h2></div></div><div class="configuration-rollback"><label class="form-field"><span>目标版本</span><input v-model="rollbackRevision" placeholder="例如 cfg-000001"/></label><button class="btn-secondary" type="button" :disabled="busy||!store.can('config.publish')" @click="rollback">恢复此版本</button></div></section>
  <div v-if="message" class="inline-alert" :data-state="validation&&!validation.valid?'bad':'info'">{{message}}</div>
</section></template>
