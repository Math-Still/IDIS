<script setup lang="ts">
import { onMounted, ref } from 'vue';
import type { PlatformConfigSnapshot, PlatformRule } from '@smart-factory/domain';
import { useSiteStore } from '../stores/site';
import PageHeader from '../components/PageHeader.vue';

const store=useSiteStore();
const snapshot=ref<PlatformConfigSnapshot|null>(null);
const rules=ref<PlatformRule[]>([]);
const reason=ref('');
const busy=ref(false);
const message=ref<string|null>(null);
const error=ref<string|null>(null);

async function load(){
  await store.refreshPlatformConfig();
  snapshot.value=await store.getPlatformConfigSnapshot();
  rules.value=(snapshot.value.rules??[]).map((r)=>structuredClone(r));
}
async function publish(){
  if(!snapshot.value) return;
  if(!reason.value.trim()){error.value='请填写规则变更原因。';return;}
  busy.value=true;error.value=null;message.value=null;
  try{
    const next=structuredClone(snapshot.value); next.rules=rules.value.map((r)=>structuredClone(r));
    const result=await store.createValidatePublishConfig(next,reason.value.trim());
    if(!result.validation.valid){error.value=result.validation.errors.map((e)=>`${e.code}: ${e.message}`).join('；');return;}
    message.value=`规则配置已发布：${result.publish?.revision??'已提交'}`; reason.value=''; await load();
  }catch(cause){error.value=cause instanceof Error?cause.message:String(cause);}finally{busy.value=false;}
}
onMounted(()=>void load());
</script>

<template><section class="operator-page">
  <PageHeader title="规则管理" eyebrow="报警规则" description="设置监测阈值、恢复条件、持续时间和报警级别。"/>
  <div v-if="error" class="inline-alert" data-state="bad">{{error}}</div>
  <div v-if="message" class="inline-alert" data-state="good">{{message}}</div>
  <section class="hmi-panel"><div class="table-card"><table><thead><tr><th>规则</th><th>测点</th><th>触发</th><th>恢复</th><th>持续</th><th>质量门槛</th><th>启用</th></tr></thead><tbody>
    <tr v-for="rule in rules" :key="rule.ruleId">
      <td><strong>{{rule.title}}</strong><small class="table-sub">{{rule.ruleId}} · v{{rule.version}}</small></td>
      <td>{{rule.point.deviceId}} / {{rule.point.pointId}}</td>
      <td><input v-model.number="rule.triggerThreshold" type="number" step="0.1" class="compact-input"></td>
      <td><input v-model.number="rule.recoveryThreshold" type="number" step="0.1" class="compact-input"></td>
      <td><input v-model.number="rule.durationMs" type="number" min="0" step="100" class="compact-input"> ms</td>
      <td>{{rule.minimumQuality}}</td>
      <td><input v-model="rule.enabled" type="checkbox"></td>
    </tr>
    <tr v-if="!rules.length"><td colspan="7" class="table-empty">当前运行配置没有规则</td></tr>
  </tbody></table></div></section>
  <section class="hmi-panel"><label class="form-field"><span>发布原因</span><textarea v-model="reason" rows="2" maxlength="500" placeholder="填写阈值、持续时间或启用状态的变更原因"></textarea></label><div class="table-actions"><button class="btn-secondary" type="button" :disabled="busy" @click="load">重新加载</button><button class="btn-primary" type="button" :disabled="busy||!store.can('config.publish')" @click="publish">{{busy?'发布中':'校验并发布规则'}}</button></div></section>
</section></template>
