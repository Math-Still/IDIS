<script setup lang="ts">
import {computed,reactive,ref} from 'vue';
import {useRoute} from 'vue-router';
import {useDevice} from '../composables/useDevice';
import {useSiteStore} from '../stores/site';
import StatusPill from '../components/StatusPill.vue';
import PageHeader from '../components/PageHeader.vue';
import CommandPreviewDialog from '../components/CommandPreviewDialog.vue';
import {deviceProtocol,deviceStatusLabel,qualityLabel,severityLabel,type Command,type CommandCapability,type CommandPreviewV2,type CommandState,type TelemetryPoint} from '@smart-factory/domain';
import {formatDurationSeconds,formatInteger,formatLatency,formatRate,formatTelemetryText} from '../utils/industrial-format';
import {alarmStateLabel,commandActionLabel,commandFeedbackLabel,commandOperatorSummary,commandStateLabel} from '../utils/operator-labels';
const route=useRoute();const store=useSiteStore();const id=computed(()=>String(route.params.id));
const{device,telemetry,alarms,communication}=useDevice(()=>id.value);
const issuingAction=ref<string|null>(null);const commandInputs=reactive<Record<string,string>>({});const commandReasons=reactive<Record<string,string>>({});const commandError=ref<string|null>(null);
const pendingPreview=ref<CommandPreviewV2|null>(null);const previewBusy=ref(false);const previewError=ref<string|null>(null);
const reconcileTarget=ref<Command|null>(null);const reconcileConclusion=ref<'CONFIRMED_APPLIED'|'CONFIRMED_NOT_APPLIED'|'INCONCLUSIVE'>('INCONCLUSIVE');const reconcileEvidence=ref('');const reconcileAllowRetry=ref(false);const reconcileBusy=ref(false);const reconcileError=ref<string|null>(null);
const commands=computed(()=>store.dashboard.commands.filter(c=>c.deviceId===id.value).sort((a,b)=>b.issuedAt-a.issuedAt));
const commandTone=(state:CommandState):'good'|'warn'|'bad'|'muted'=>state==='CONFIRMED'?'good':['REJECTED','FAILED','EXPIRED'].includes(state)?'bad':['TIMEOUT','OUTCOME_UNKNOWN'].includes(state)?'warn':['ISSUED','RECEIVED','ACCEPTED','EXECUTING','APPLIED'].includes(state)?'warn':'muted';
const deviceTone=computed<'good'|'warn'|'info'|'muted'>(()=>device.value?.status==='ONLINE'?'good':device.value?.status==='DEGRADED'?'warn':device.value?.status==='MAINTENANCE'?'info':'muted');
const availableCommands=computed(()=>Object.values(device.value?.commandCapabilities??{}).filter((cap)=>cap.executionClass!=='EMERGENCY'));
const emergencyCommandCount=computed(()=>Object.values(device.value?.commandCapabilities??{}).filter((cap)=>cap.executionClass==='EMERGENCY').length);
function schemaType(capability:CommandCapability):string{return typeof capability.parameterSchema.type==='string'?capability.parameterSchema.type:'none';}
function schemaNumber(capability:CommandCapability,key:'minimum'|'maximum'):number|undefined{const v=capability.parameterSchema[key];return typeof v==='number'&&Number.isFinite(v)?v:undefined;}
function commandRetryReleased(command:Command):boolean{return Boolean(command.reconciliations?.length&&command.reconciliations[command.reconciliations.length-1]?.allowRetry);}
function blockingUnknownCommand(action:string):Command|undefined{return commands.value.find(command=>command.action===action&&command.state==='OUTCOME_UNKNOWN'&&!commandRetryReleased(command));}
function commandAllowed(capability:CommandCapability):boolean{return store.can(capability.requiredPermission)&&device.value?.status!=='OFFLINE'&&!blockingUnknownCommand(capability.actionId);}
function requestedValue(capability:CommandCapability):unknown{
  const type=schemaType(capability);const raw=commandInputs[capability.actionId]??'';
  if(type==='none')return undefined;const required=capability.parameterSchema.required===true;
  if(required&&!raw.trim())throw new Error('该操作必须填写参数，空值不会自动转换为 0。');
  if(type==='number'){const value=Number(raw);if(!raw.trim()||!Number.isFinite(value))throw new Error('请输入有效数值。');const min=schemaNumber(capability,'minimum'),max=schemaNumber(capability,'maximum');if(min!==undefined&&value<min)throw new Error(`数值不能低于 ${min}。`);if(max!==undefined&&value>max)throw new Error(`数值不能高于 ${max}。`);return value;}
  if(type==='integer'){const value=Number(raw);if(!raw.trim()||!Number.isInteger(value))throw new Error('请输入整数。');const min=schemaNumber(capability,'minimum'),max=schemaNumber(capability,'maximum');if(min!==undefined&&value<min)throw new Error(`数值不能低于 ${min}。`);if(max!==undefined&&value>max)throw new Error(`数值不能高于 ${max}。`);return value;}
  if(type==='boolean'){if(raw!=='true'&&raw!=='false')throw new Error('请明确选择“是”或“否”。');return raw==='true';}
  if(type==='string'){if(required&&!raw.trim())throw new Error('请输入操作参数。');return raw.trim();}return raw;
}
async function executeCapability(capability:CommandCapability){
  if(!device.value||issuingAction.value||!commandAllowed(capability))return;commandError.value=null;previewError.value=null;
  try{const value=requestedValue(capability);const reason=(commandReasons[capability.actionId]??'').trim();if(!reason)throw new Error('请填写本次操作原因。');issuingAction.value=capability.actionId;
    if(store.dataSource==='BACKEND') pendingPreview.value=await store.previewCommandV2({deviceId:device.value.id,action:capability.actionId,requestedValue:value,reason});
    else pendingPreview.value={previewId:`local-${Date.now()}`,deviceId:device.value.id,deviceName:device.value.name,action:capability.actionId,displayName:capability.displayName,requestedValue:value,reason,requiredPermission:capability.requiredPermission,executionClass:capability.executionClass,parameterSchema:capability.parameterSchema,configRevision:'local',deviceStatus:device.value.status,createdAt:Date.now(),expiresAt:Date.now()+30000,adapterWriteReady:true,adapterDetail:'当前使用内置设备源。'};
  }catch(cause){commandError.value=cause instanceof Error?cause.message:String(cause);}finally{issuingAction.value=null;}
}
async function confirmPreview(){if(!pendingPreview.value)return;previewBusy.value=true;previewError.value=null;try{const preview=pendingPreview.value;if(store.dataSource==='BACKEND')await store.issuePreviewedCommandV2(preview);else await store.issueCommand({deviceId:preview.deviceId,action:preview.action,requestedValue:preview.requestedValue,reason:preview.reason});pendingPreview.value=null;}catch(cause){previewError.value=cause instanceof Error?cause.message:String(cause);}finally{previewBusy.value=false;}}
function cancelPreview(){if(!previewBusy.value){pendingPreview.value=null;previewError.value=null;}}
function deliveryLabel(command:Command):string{const labels:Record<string,string>={NOT_SENT:'未发送',REJECTED:'设备拒绝',POSSIBLY_APPLIED:'状态待确认',APPLIED:'已发送'};return command.deliveryCertainty?labels[command.deliveryCertainty]??command.deliveryCertainty:'—';}
async function openReconciliation(command:Command){reconcileError.value=null;try{reconcileTarget.value=store.dataSource==='BACKEND'?await store.refreshCommandV2(command.id):command;reconcileConclusion.value='INCONCLUSIVE';reconcileEvidence.value='';reconcileAllowRetry.value=false;}catch(cause){commandError.value=cause instanceof Error?cause.message:String(cause);}}
function closeReconciliation(){if(!reconcileBusy.value){reconcileTarget.value=null;reconcileError.value=null;}}
async function submitReconciliation(){if(!reconcileTarget.value)return;const evidence=reconcileEvidence.value.trim();if(!evidence){reconcileError.value='请填写核查记录。';return;}reconcileBusy.value=true;reconcileError.value=null;try{await store.reconcileCommandV2(reconcileTarget.value.id,{conclusion:reconcileConclusion.value,evidence,allowRetry:reconcileAllowRetry.value});reconcileTarget.value=null;}catch(cause){reconcileError.value=cause instanceof Error?cause.message:String(cause);}finally{reconcileBusy.value=false;}}
function formatPoint(point:TelemetryPoint){return formatTelemetryText(point);}
</script>
<template>
<section v-if="device" class="operator-page" :data-scenario="device.scenario">
  <PageHeader :title="device.name" eyebrow="设备详情" back-to="/devices" back-label="返回设备列表" :description="`${device.location} · ${deviceProtocol(device)}`"><template #actions><StatusPill :tone="deviceTone" :label="deviceStatusLabel[device.status]"/></template></PageHeader>

  <div class="faceplate-layout">
    <section class="hmi-panel device-faceplate-main" :data-state="device.status.toLowerCase()">
      <div class="hmi-panel-head"><div><span>设备状态</span><h2>{{device.id}}</h2></div><StatusPill :tone="deviceTone" :label="deviceStatusLabel[device.status]"/></div>
      <dl class="faceplate-identity"><div><dt>位置</dt><dd>{{device.location}}</dd></div><div><dt>通信协议</dt><dd>{{deviceProtocol(device)}}</dd></div><div><dt>设备编号</dt><dd>{{device.id}}</dd></div><div><dt>最后数据</dt><dd>{{formatDurationSeconds(device.lastSeen)}}</dd></div></dl>
      <div class="faceplate-values">
        <div v-for="point in telemetry" :key="point.pointId" class="faceplate-value" :data-quality="point.quality.toLowerCase()"><span>{{point.label}}</span><strong>{{formatPoint(point)}}</strong><small>{{qualityLabel[point.quality]}}</small></div>
        <div v-if="!telemetry.length" class="card-empty">暂无实时数据</div>
      </div>
    </section>

    <section class="hmi-panel device-comm-faceplate">
      <div class="hmi-panel-head"><div><span>通信状态</span><h2>通信健康</h2></div><small>{{store.dataOriginLabel}} · {{communication?qualityLabel[communication.quality]:'暂无数据'}}</small></div>
      <dl v-if="communication" class="faceplate-kv"><div><dt>P50</dt><dd>{{formatLatency(communication.latencyMsP50)}}</dd></div><div><dt>P95</dt><dd>{{formatLatency(communication.latencyMsP95)}}</dd></div><div><dt>P99</dt><dd>{{formatLatency(communication.latencyMsP99)}}</dd></div><div><dt>抖动</dt><dd>{{formatLatency(communication.jitterMs)}}</dd></div><div><dt>消息率</dt><dd>{{formatRate(communication.messageRatePerMin)}}</dd></div><div><dt>错误</dt><dd>{{formatInteger(communication.errorCount)}}</dd></div><div><dt>重连</dt><dd>{{formatInteger(communication.reconnectCount)}}</dd></div><div><dt>质量</dt><dd>{{qualityLabel[communication.quality]}}</dd></div></dl>
      <div v-else class="card-empty">暂无通信数据</div>
      <p v-if="store.dataOrigin!=='REAL_DEVICE'" class="panel-note">数据来源：{{store.dataOriginLabel}}。</p>
    </section>
  </div>

  <div class="two-col device-secondary-layout">
    <section class="hmi-panel"><div class="hmi-panel-head"><div><span>安全事件</span><h2>相关报警</h2></div><small>{{alarms.length}} 条记录</small></div><div class="simple-list"><div v-for="alarm in alarms" :key="alarm.id"><strong>{{alarm.title}}</strong><span>{{severityLabel[alarm.severity]}} · {{alarmStateLabel[alarm.state]}} · {{new Date(alarm.raisedAt).toLocaleString('zh-CN',{hour12:false})}}</span></div><div v-if="!alarms.length" class="card-empty">暂无相关报警</div></div></section>
    <section class="hmi-panel command-panel"><div class="hmi-panel-head"><div><span>设备操作</span><h2>可用设备操作</h2></div><small>{{availableCommands.length}} 项</small></div>
      <div class="command-capability-list">
        <div v-for="capability in availableCommands" :key="capability.actionId" class="command-capability-row">
          <div><strong>{{capability.displayName}}</strong><small>{{capability.executionClass==='REALTIME'?'实时执行域':'普通执行域'}}</small></div>
          <input v-if="['number','integer','string'].includes(schemaType(capability))" v-model="commandInputs[capability.actionId]" class="command-value-input" :type="['number','integer'].includes(schemaType(capability))?'number':'text'" :min="schemaNumber(capability,'minimum')" :max="schemaNumber(capability,'maximum')" placeholder="输入操作值" />
          <select v-else-if="schemaType(capability)==='boolean'" v-model="commandInputs[capability.actionId]" class="command-value-input"><option value="" disabled>请选择</option><option value="true">是</option><option value="false">否</option></select>
          <input v-model="commandReasons[capability.actionId]" class="command-value-input" type="text" placeholder="填写本次操作原因" />
          <button type="button" class="btn-secondary" :disabled="issuingAction!==null||!commandAllowed(capability)" @click="executeCapability(capability)">{{issuingAction===capability.actionId?'执行中':capability.displayName}}</button><small v-if="blockingUnknownCommand(capability.actionId)" class="table-sub">存在结果待确认操作 {{blockingUnknownCommand(capability.actionId)?.id}}，核查前禁止重复操作</small>
        </div>
        <div v-if="!availableCommands.length" class="card-empty">当前设备未登记可由平台执行的非安全操作</div>
        <p v-if="commandError" class="panel-note">操作未提交：{{commandError}}</p>
      </div>
</section>
  </div>

  <section class="hmi-panel command-history-panel"><div class="hmi-panel-head"><div><span>操作记录</span><h2>设备命令记录</h2></div><small>{{commands.length}} 条记录</small></div><div class="table-card command-table"><table><thead><tr><th>命令</th><th>状态</th><th>发送状态</th><th>设备反馈</th><th>操作员</th><th>更新时间</th><th>详情</th></tr></thead><tbody><tr v-for="command in commands" :key="command.id"><td><strong class="table-primary-cell">{{commandActionLabel(command.action)}}</strong></td><td><StatusPill :tone="commandTone(command.state)" :label="commandStateLabel[command.state]"/></td><td>{{deliveryLabel(command)}}</td><td>{{commandFeedbackLabel(command.feedbackStatus)}}<small v-if="command.feedbackAt" class="table-sub">{{new Date(command.feedbackAt).toLocaleTimeString('zh-CN',{hour12:false})}}</small></td><td>{{command.operator||'—'}}</td><td class="table-time">{{new Date(command.updatedAt).toLocaleString('zh-CN',{hour12:false})}}</td><td>{{commandOperatorSummary(command)}}<small v-if="command.reconciliations?.length" class="table-sub">已补充 {{command.reconciliations.length}} 条核查记录</small><button v-if="command.state==='OUTCOME_UNKNOWN'&&store.dataSource==='BACKEND'&&store.can('command.reconcile')" type="button" class="btn-link" @click="openReconciliation(command)">保存核查</button></td></tr><tr v-if="!commands.length"><td colspan="7"><div class="table-empty">暂无设备命令记录</div></td></tr></tbody></table></div></section>
  <CommandPreviewDialog v-if="pendingPreview" :preview="pendingPreview" :busy="previewBusy" :error="previewError" @confirm="confirmPreview" @cancel="cancelPreview"/>

  <div v-if="reconcileTarget" class="hmi-modal-backdrop" @click.self="closeReconciliation"><section class="hmi-modal" role="dialog" aria-modal="true" aria-label="命令结果人工核查"><header><div><span>结果核查</span><strong>{{commandActionLabel(reconcileTarget.action)}}</strong></div><button class="modal-close" type="button" :disabled="reconcileBusy" @click="closeReconciliation">×</button></header><div class="identity-confirm"><span>当前记录</span><strong>{{commandStateLabel[reconcileTarget.state]}}</strong><small>{{reconcileTarget.id}}</small></div><label class="form-field"><span>核查结论</span><select v-model="reconcileConclusion"><option value="INCONCLUSIVE">仍无法确定</option><option value="CONFIRMED_APPLIED">确认已执行</option><option value="CONFIRMED_NOT_APPLIED">确认未执行</option></select></label><label class="form-field"><span>核查记录</span><textarea v-model="reconcileEvidence" rows="4" maxlength="2000" placeholder="填写设备回读、日志或人员核验情况"></textarea></label><label v-if="reconcileConclusion!=='INCONCLUSIVE'" class="form-field"><span>后续重试</span><select v-model="reconcileAllowRetry"><option :value="false">仍禁止重试</option><option :value="true">允许重新操作</option></select></label><div v-if="reconcileError" class="inline-alert" data-state="bad">{{reconcileError}}</div><footer><button class="btn-secondary" type="button" :disabled="reconcileBusy" @click="closeReconciliation">取消</button><button class="btn-primary" type="button" :disabled="reconcileBusy" @click="submitReconciliation">{{reconcileBusy?'记录中':'保存核查'}}</button></footer></section></div>
</section>
<section v-else class="operator-page"><PageHeader title="设备不存在" eyebrow="设备详情" back-to="/devices" back-label="返回设备列表" description="当前数据集中没有找到该设备。"/></section>
</template>
