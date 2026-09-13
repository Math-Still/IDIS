<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, shallowRef } from 'vue';
import { useRouter } from 'vue-router';
import { type CommandCapability, type CommandPreviewV2, type Device, type PointDefinition, type TelemetryPoint } from '@smart-factory/domain';
import { useSiteStore } from '../stores/site';
import AppIcon from '../components/AppIcon.vue';
import CommandPreviewDialog from '../components/CommandPreviewDialog.vue';
import CopperSmelterTwin from '../components/digital-twin/CopperSmelterTwin.vue';
import SemiCircularGauge from '../components/gauges/SemiCircularGauge.vue';
import LinearGauge from '../components/gauges/LinearGauge.vue';
import StatusMatrix from '../components/gauges/StatusMatrix.vue';
import { gaugeScale, gaugeThresholds, gaugeTone, thresholdSummary } from '../components/gauges/gauge.utils';
import type { GaugeThresholds, GaugeTone } from '../components/gauges/gauge.types';
import { TWIN_ZONES } from '../components/digital-twin/twin.config';
import { buildTwinZoneRuntime, twinStatusLabel } from '../components/digital-twin/twin.runtime';
import type { TwinStatus, TwinZoneRuntime } from '../components/digital-twin/twin.types';
import { formatInteger, formatNumber, formatTelemetry, publicSiteLabel } from '../utils/industrial-format';

const store = useSiteStore();
const dashboard = shallowRef(JSON.parse(JSON.stringify(store.dashboard)) as typeof store.dashboard);
const router = useRouter();
const now = ref(new Date());
const isFullscreen = ref(false);
const selectedZoneId = ref('converting');
let clockTimer: ReturnType<typeof setInterval> | null = null;

const numericTelemetry = computed(() => dashboard.value.telemetry.filter((point): point is TelemetryPoint & { value:number } => typeof point.value === 'number'));
const deviceMap = computed(() => new Map(dashboard.value.devices.map((device) => [device.id, device])));
const activeAlarms = computed(() => [...dashboard.value.alarms]
  .filter((alarm) => alarm.state !== 'CLEARED')
  .sort((a,b) => (a.severity === 'CRITICAL' ? -3 : a.severity === 'WARNING' ? -2 : -1) - (b.severity === 'CRITICAL' ? -3 : b.severity === 'WARNING' ? -2 : -1) || b.raisedAt - a.raisedAt));
const criticalCount = computed(() => activeAlarms.value.filter((alarm) => alarm.severity === 'CRITICAL').length);
const warningCount = computed(() => activeAlarms.value.filter((alarm) => alarm.severity === 'WARNING').length);
const abnormalDevices = computed(() => dashboard.value.devices.filter((device) => device.status !== 'ONLINE'));
const overallState = computed(() => criticalCount.value ? 'ALARM' : warningCount.value || abnormalDevices.value.length || store.degradedLinks ? 'ATTENTION' : 'NORMAL');
const overallLabel = computed(() => overallState.value === 'ALARM' ? '存在严重报警' : overallState.value === 'ATTENTION' ? '运行需关注' : '全厂运行正常');
const dataAgeMs = computed(() => Math.max(0, now.value.getTime() - dashboard.value.generatedAt));
const dataDelay = computed(() => dataAgeMs.value > 10_000);
const twinZones = computed(() => buildTwinZoneRuntime(TWIN_ZONES, dashboard.value.devices, dashboard.value.alarms, dashboard.value.telemetry));
const selectedZone = computed(() => twinZones.value.find((item) => item.zone.id === selectedZoneId.value) ?? twinZones.value[0]);
const processZones = computed(() => twinZones.value.filter((item) => ['raw-material','smelting','converting','refining','casting','acid'].includes(item.zone.id)));

const goodsTotals = computed(() => numericTelemetry.value.filter((point) => deviceMap.value.get(point.deviceId)?.scenario === 'goods_counting' && point.pointId === 'totalCount'));
const goodsTargets = computed(() => numericTelemetry.value.filter((point) => deviceMap.value.get(point.deviceId)?.scenario === 'goods_counting' && point.pointId === 'target'));
const goodsRates = computed(() => numericTelemetry.value.filter((point) => deviceMap.value.get(point.deviceId)?.scenario === 'goods_counting' && point.pointId === 'rate'));
const transferTotal = computed(() => goodsTotals.value.reduce((sum, point) => sum + point.value, 0));
const transferTarget = computed(() => goodsTargets.value.reduce((sum, point) => sum + point.value, 0));
const transferProgress = computed(() => transferTarget.value > 0 ? Math.min(100, transferTotal.value / transferTarget.value * 100) : 0);
const productionRate = computed(() => goodsRates.value.reduce((sum, point) => sum + point.value, 0));
const onlineRate = computed(() => dashboard.value.devices.length ? store.onlineDevices / dashboard.value.devices.length * 100 : 0);
const currentHeatText = computed(() => store.dataSource === 'DEMO' ? '演示炉次 D-2856' : '炉次数据未接入');

type GaugeViewModel = {
  key:string;
  label:string;
  value:number|null;
  displayValue:string;
  unit:string;
  min:number;
  max:number;
  thresholds:GaugeThresholds;
  tone:GaugeTone;
  detail:string;
};
type GaugeProps = Omit<GaugeViewModel, 'key'>;
function gaugeProps({ key, ...props }: GaugeViewModel): GaugeProps {
  void key;
  return props;
}

function pointDefinition(point?:TelemetryPoint): PointDefinition | undefined {
  if (!point) return undefined;
  return deviceMap.value.get(point.deviceId)?.pointDefinitions?.find((definition) => definition.pointId === point.pointId);
}
function numericPoint(deviceId:string, pointId:string): (TelemetryPoint & { value:number }) | undefined {
  const point = dashboard.value.telemetry.find((item) => item.deviceId === deviceId && item.pointId === pointId);
  return point && typeof point.value === 'number' && Number.isFinite(point.value) ? point as TelemetryPoint & { value:number } : undefined;
}
function gaugeLabel(point:TelemetryPoint): string {
  const map:Record<string,string> = {
    'safety-01:level':'转炉区气体浓度',
    'safety-01:so2':'烟气 SO₂',
    'safety-01:o2':'烟气 O₂',
    'safety-01:co':'烟气 CO',
    'safety-01:pressure':'炉区压力',
    'safety-02:level':'精炼区气体浓度',
    'env-01:temperature':'精矿库温度',
    'env-01:humidity':'精矿库湿度',
    'env-02:temperature':'库区 B 温度',
    'env-02:humidity':'库区 B 湿度',
    'motion-01:distance':'AGV 前向距离',
    'motion-01:speed':'AGV 运行速度'
  };
  return map[`${point.deviceId}:${point.pointId}`] ?? point.label;
}
function toGauge(point:TelemetryPoint & { value:number }): GaugeViewModel {
  const definition = pointDefinition(point);
  const thresholds = gaugeThresholds(definition);
  const scale = gaugeScale(point, definition);
  const formatted = metricDisplay(point);
  return {
    key:`${point.deviceId}:${point.pointId}`,
    label:gaugeLabel(point),
    value:point.value,
    displayValue:formatted.value,
    unit:formatted.unit,
    min:scale.min,
    max:scale.max,
    thresholds,
    tone:gaugeTone(point.value, point.quality, thresholds),
    detail:thresholdSummary(thresholds, point.unit ?? '')
  };
}
const processGaugeModels = computed<GaugeViewModel[]>(() => {
  const preferredKeys = [
    ['safety-01','so2'], ['safety-01','o2'], ['safety-01','co'], ['env-01','temperature'], ['safety-01','pressure'], ['safety-02','level'], ['env-01','humidity'], ['motion-01','distance']
  ] as const;
  const selectedIds = new Set(selectedZone.value?.devices.map((device) => device.id) ?? []);
  const selected = numericTelemetry.value.filter((point) => selectedIds.has(point.deviceId) && ['level','so2','o2','co','pressure','temperature','humidity','distance','speed'].includes(point.pointId));
  const preferred = preferredKeys.map(([deviceId, pointId]) => numericPoint(deviceId, pointId)).filter((item): item is TelemetryPoint & { value:number } => Boolean(item));
  const seen = new Set<string>();
  return [...selected, ...preferred]
    .filter((point) => { const key=`${point.deviceId}:${point.pointId}`; if (seen.has(key)) return false; seen.add(key); return true; })
    .slice(0,6)
    .map(toGauge);
});
const completionGauge = computed<GaugeViewModel>(() => ({
  key:'production:completion', label:'计划完成率', value:transferTarget.value > 0 ? transferProgress.value : null,
  displayValue:transferTarget.value > 0 ? formatNumber(transferProgress.value,{decimals:1}) : '--', unit:'%', min:0, max:100,
  thresholds:{}, tone:'info', detail:transferTarget.value > 0 ? `班次目标 ${formatInteger(transferTarget.value,'--')} 件` : '班次目标未配置'
}));
const productionRateGauge = computed<GaugeViewModel>(() => {
  const value = productionRate.value;
  const max = Math.max(10, Math.ceil(Math.max(value * 1.35, 1) / 10) * 10);
  return { key:'production:rate', label:'当前转运速率', value, displayValue:formatNumber(value,{decimals:1}), unit:'件/min', min:0, max, thresholds:{}, tone:'info', detail:'成品转运' };
});
const onlineRateGauge = computed<GaugeViewModel>(() => ({
  key:'production:online', label:'设备在线率', value:dashboard.value.devices.length ? onlineRate.value : null,
  displayValue:dashboard.value.devices.length ? formatNumber(onlineRate.value,{decimals:1}) : '--', unit:'%', min:0, max:100,
  thresholds:{}, tone:abnormalDevices.value.length ? 'warning' : 'normal', detail:`${store.onlineDevices}/${dashboard.value.devices.length} 在线`
}));

const selectedZoneOnlineGauge = computed<GaugeViewModel>(() => {
  const total = selectedZone.value?.devices.length ?? 0;
  const online = selectedZone.value?.onlineCount ?? 0;
  const value = total ? online / total * 100 : null;
  return {
    key:'zone:online', label:'当前区域在线率', value,
    displayValue:value == null ? '--' : formatNumber(value,{decimals:1}), unit:'%', min:0, max:100,
    thresholds:{}, tone:value == null ? 'offline' : value >= 99.9 ? 'normal' : value >= 80 ? 'warning' : 'alarm',
    detail:total ? `${online}/${total} 在线` : '当前区域无绑定设备'
  };
});
const gasBottomGauges = computed<GaugeViewModel[]>(() => [numericPoint('safety-01','level'), numericPoint('safety-02','level')]
  .filter((item): item is TelemetryPoint & { value:number } => Boolean(item)).map(toGauge));
const processStatusItems = computed(() => processZones.value.map((item) => ({
  key:item.zone.id, label:item.zone.shortName, state:item.devices.length ? item.status : 'offline',
  value:item.devices.length ? `${item.onlineCount}/${item.devices.length}` : '--'
})));

const keyDevices = computed(() => {
  const preferred = ['safety-01','safety-02','motion-01','count-01'];
  const selected = preferred.map((id) => deviceMap.value.get(id)).filter((item): item is Device => Boolean(item));
  return selected.length ? selected : dashboard.value.devices.slice(0,4);
});




const controlDevice = computed(() => dashboard.value.devices.find((device) => device.id === 'safety-01')
  ?? dashboard.value.devices.find((device) => Object.values(device.commandCapabilities ?? {}).some((capability) => capability.executionClass === 'EMERGENCY')));
const controlCaps = computed(() => Object.values(controlDevice.value?.commandCapabilities ?? {}).filter((capability) => ['device.start','device.stop','device.emergencyStop'].includes(capability.actionId)));
const controlFeedback = computed(() => controlDevice.value
  ? dashboard.value.telemetry.find((point) => point.deviceId === controlDevice.value?.id && point.pointId === 'fanRunning')
  : undefined);
const controlFeedbackLabel = computed(() => controlFeedback.value?.value === true ? '运行' : controlFeedback.value?.value === false ? '停止' : '状态未知');

const selectedControl = ref<CommandCapability | null>(null);
const controlReason = ref('');
const controlError = ref('');
const controlBusy = ref(false);
const pendingPreview = ref<CommandPreviewV2 | null>(null);
const previewError = ref('');

function metricDisplay(point?: TelemetryPoint){
  const formatted = formatTelemetry(point);
  const invalid = ['NO_DATA','INVALID','FROZEN','OFFLINE','UNKNOWN'].includes(formatted.status);
  return { value: invalid ? '--' : formatted.valueText, unit: invalid ? '' : formatted.unitText };
}

function deviceStateLabel(status:string){ return status === 'ONLINE' ? '在线' : status === 'DEGRADED' ? '降级' : status === 'MAINTENANCE' ? '维护' : '离线'; }
function deviceStateTone(status:string){ return status === 'ONLINE' ? 'running' : status === 'DEGRADED' ? 'warning' : status === 'MAINTENANCE' ? 'maintenance' : 'offline'; }
function zoneStateTone(status:TwinStatus){ return status; }
function onSelectZone(zone:TwinZoneRuntime){ selectedZoneId.value = zone.zone.id; }
function openControl(capability: CommandCapability){ selectedControl.value = capability; controlReason.value = ''; controlError.value = ''; }
async function prepareControl(){
  if (!selectedControl.value || !controlDevice.value) return;
  const reason = controlReason.value.trim();
  if (!reason) { controlError.value = '请填写本次操作原因。'; return; }
  controlBusy.value = true; controlError.value = '';
  try {
    pendingPreview.value = await store.previewCommandV2({ deviceId:controlDevice.value.id, action:selectedControl.value.actionId, reason });
    selectedControl.value = null;
  } catch (error) { controlError.value = error instanceof Error ? error.message : String(error); }
  finally { controlBusy.value = false; }
}
async function confirmPreview(){
  if (!pendingPreview.value) return;
  controlBusy.value = true; previewError.value = '';
  try { await store.issuePreviewedCommandV2(pendingPreview.value); pendingPreview.value = null; }
  catch (error) { previewError.value = error instanceof Error ? error.message : String(error); }
  finally { controlBusy.value = false; }
}
function controlAllowed(capability: CommandCapability){ return store.can(capability.requiredPermission) && controlDevice.value?.status !== 'OFFLINE'; }
async function toggleFullscreen(){
  try {
    if (!document.fullscreenElement) await document.documentElement.requestFullscreen();
    else await document.exitFullscreen();
  } catch { /* browser may reject fullscreen without a direct user gesture */ }
}
function updateFullscreen(){ isFullscreen.value = Boolean(document.fullscreenElement); }
function handleKeydown(event: KeyboardEvent){ if (event.key === 'Escape' && !document.fullscreenElement) void router.push('/'); }

onMounted(() => {
  clockTimer = setInterval(() => { now.value = new Date(); dashboard.value = JSON.parse(JSON.stringify(store.dashboard)); }, 1000);
  window.addEventListener('keydown', handleKeydown);
  document.addEventListener('fullscreenchange', updateFullscreen);
});
onUnmounted(() => {
  if (clockTimer) clearInterval(clockTimer);
  window.removeEventListener('keydown', handleKeydown);
  document.removeEventListener('fullscreenchange', updateFullscreen);
});
</script>

<template>
  <div class="twin-screen twin-screen--executive" :data-overall="overallState.toLowerCase()">
    <header class="twin-screen-header">
      <div class="twin-screen-title">
        <span>铜冶炼智能工厂</span>
        <strong>生产运行总览</strong>
        <small>{{publicSiteLabel(dashboard.siteName)}}</small>
      </div>
      <div class="twin-screen-context">
        <div><span>运行状态</span><strong :data-state="overallState.toLowerCase()"><i></i>{{overallLabel}}</strong></div>
        <div><span>在线设备</span><strong>{{store.onlineDevices}} / {{dashboard.devices.length}}</strong></div>
        <div><span>活动告警</span><strong :data-alert="activeAlarms.length>0">{{activeAlarms.length}}</strong></div>
        <div><span>成品转运</span><strong>{{formatInteger(transferTotal,'--')}} 件</strong></div>
      </div>
      <div class="twin-screen-clock">
        <strong>{{now.toLocaleTimeString('zh-CN',{hour12:false})}}</strong>
        <span>{{now.toLocaleDateString('zh-CN')}}</span>
        <small :data-delay="dataDelay">{{dataDelay?'数据延迟':'实时更新'}}</small>
      </div>
      <div class="twin-screen-actions">
        <button type="button" @click="toggleFullscreen"><AppIcon name="external" :size="16"/>{{isFullscreen?'退出全屏':'全屏'}}</button>
        <RouterLink to="/"><AppIcon name="external" :size="16"/>返回系统</RouterLink>
      </div>
    </header>

    <main class="twin-screen-main twin-screen-main--executive">
      <aside class="twin-side twin-side-left">
        <section class="twin-info-section production-summary executive-production">
          <header><span>生产总览</span><b>PRODUCTION</b></header>
          <div class="executive-production-body">
            <SemiCircularGauge v-bind="gaugeProps(completionGauge)" />
            <div class="executive-kpi-stack">
              <div><span>当前转运</span><strong>{{formatInteger(transferTotal,'--')}}<small>件</small></strong><small class="executive-production-context">{{currentHeatText}} · {{selectedZone?.zone.shortName ?? '工艺阶段未接入'}}</small></div>
              <LinearGauge v-bind="gaugeProps(productionRateGauge)" />
            </div>
          </div>
        </section>

        <section class="twin-info-section executive-process-overview">
          <header><span>工艺状态</span><b>{{selectedZone?.zone.shortName ?? 'PROCESS'}}</b></header>
          <div class="executive-process-rail">
            <button v-for="item in processZones" :key="item.zone.id" type="button" :data-status="zoneStateTone(item.status)" :class="{'is-active':selectedZoneId===item.zone.id}" @click="selectedZoneId=item.zone.id">
              <i></i><span>{{item.zone.shortName}}</span><b>{{item.devices.length?twinStatusLabel(item.status):'--'}}</b>
            </button>
          </div>
        </section>

        <section class="twin-info-section key-equipment executive-key-equipment">
          <header><span>关键设备</span><b>EQUIPMENT</b></header>
          <div class="key-equipment-list">
            <RouterLink v-for="device in keyDevices.slice(0,3)" :key="device.id" :to="`/devices/${device.id}`" :data-status="deviceStateTone(device.status)">
              <i></i><div><strong>{{device.name}}</strong><span>{{device.location}}</span></div><b>{{deviceStateLabel(device.status)}}</b>
            </RouterLink>
          </div>
        </section>
      </aside>

      <section class="twin-center-stage twin-center-stage--hero">
        <div class="twin-center-heading">
          <div><span>核心厂区</span><strong>铜冶炼厂区总览</strong></div>
          <small>区域状态 · 实时联动</small>
        </div>
        <CopperSmelterTwin
          :devices="dashboard.devices"
          :telemetry="dashboard.telemetry"
          :alarms="dashboard.alarms"
          :active-zone-id="selectedZoneId"
          @select-zone="onSelectZone"
        />
      </section>

      <aside class="twin-side twin-side-right">
        <section class="twin-info-section process-metrics executive-process-metrics">
          <header><span>关键过程参数</span><b>{{selectedZone?.zone.shortName ?? 'PROCESS'}}</b></header>
          <div class="process-gauge-layout executive-gauge-layout">
            <div class="process-semi-gauges executive-semi-gauges">
              <SemiCircularGauge v-for="gauge in processGaugeModels.slice(0,2)" :key="gauge.key" v-bind="gaugeProps(gauge)" />
            </div>
            <div v-if="processGaugeModels.length>2" class="process-linear-gauges executive-process-lines">
              <LinearGauge v-for="gauge in processGaugeModels.slice(2,6)" :key="`linear-${gauge.key}`" v-bind="gaugeProps(gauge)" />
            </div>
            <div v-if="!processGaugeModels.length" class="process-gauge-empty">当前区域暂无有效实时参数</div>
          </div>
        </section>

        <section class="twin-info-section safety-monitor executive-safety-monitor">
          <header><span>安全与环保</span><b>SAFETY</b></header>
          <div class="executive-safety-grid">
            <div><span>严重报警</span><strong :data-alert="criticalCount>0">{{criticalCount}}</strong></div>
            <div><span>预警事项</span><strong :data-warning="warningCount>0">{{warningCount}}</strong></div>
            <div><span>异常设备</span><strong :data-warning="abnormalDevices.length>0">{{abnormalDevices.length}}</strong></div>
          </div>
        </section>

        <section class="twin-info-section twin-alarm-panel executive-alarm-panel">
          <header><span>最新告警</span><b>{{activeAlarms.length}} 条</b></header>
          <div class="twin-alarm-list">
            <article v-for="alarm in activeAlarms.slice(0,2)" :key="alarm.id" :data-severity="alarm.severity.toLowerCase()">
              <i></i><div><strong>{{alarm.title}}</strong><span>{{deviceMap.get(alarm.deviceId)?.name ?? alarm.deviceId}}</span></div><time>{{new Date(alarm.raisedAt).toLocaleTimeString('zh-CN',{hour12:false})}}</time>
            </article>
            <div v-if="!activeAlarms.length" class="twin-quiet-state"><i></i><span>当前无活动告警</span></div>
          </div>
        </section>

        <section class="twin-control-panel executive-control-panel">
          <header><div><span>核心控制</span><strong>{{controlDevice?.name ?? '安全联动设备'}}</strong></div><b><i :data-state="controlFeedback?.value===true?'running':'stopped'"></i>{{controlFeedbackLabel}}</b></header>
          <div class="twin-control-buttons">
            <button v-for="capability in controlCaps" :key="capability.actionId" type="button" :class="capability.actionId==='device.emergencyStop'?'emergency':capability.actionId==='device.stop'?'stop':'start'" :disabled="controlBusy||!controlAllowed(capability)" @click="openControl(capability)">{{capability.displayName}}</button>
          </div>
        </section>
      </aside>
    </main>

    <section class="twin-bottom-strip twin-bottom-instruments twin-bottom-instruments--executive">
      <section class="bottom-instrument-panel bottom-production-panel executive-bottom-panel">
        <header><span>产量与计划</span><b>PRODUCTION</b></header>
        <div class="executive-bottom-production">
          <SemiCircularGauge v-bind="gaugeProps(completionGauge)" compact />
          <div><span>当前转运</span><strong>{{formatInteger(transferTotal,'--')}}<small>件</small></strong><small class="executive-production-context">{{currentHeatText}} · {{selectedZone?.zone.shortName ?? '工艺阶段未接入'}}</small></div>
        </div>
      </section>

      <section class="bottom-instrument-panel executive-bottom-panel">
        <header><span>设备健康</span><b>EQUIPMENT</b></header>
        <div class="executive-bottom-lines">
          <LinearGauge v-bind="gaugeProps(onlineRateGauge)" />
          <LinearGauge v-bind="gaugeProps(selectedZoneOnlineGauge)" />
        </div>
      </section>

      <section class="bottom-instrument-panel bottom-environment-panel executive-bottom-panel">
        <header><span>安全环保</span><b>ENVIRONMENT</b></header>
        <div class="bottom-gas-gauges executive-bottom-gauges">
          <SemiCircularGauge v-for="gauge in gasBottomGauges.slice(0,2)" :key="`bottom-${gauge.key}`" v-bind="gaugeProps(gauge)" compact />
          <div v-if="!gasBottomGauges.length" class="bottom-empty-state">暂无有效监测值</div>
        </div>
      </section>

      <section class="bottom-instrument-panel bottom-process-panel executive-bottom-panel">
        <header><span>工艺状态</span><b>PROCESS</b></header>
        <StatusMatrix :items="processStatusItems" />
      </section>
    </section>

    <footer class="twin-screen-footer twin-screen-footer--executive">
      <span><i :data-state="store.realtimeState==='CONNECTED'?'normal':'warning'"></i>{{store.realtimeState==='CONNECTED'?'实时链路正常':'实时链路异常'}}</span>
      <span>{{store.dataOriginLabel}}</span>
      <span>{{new Date(dashboard.generatedAt).toLocaleTimeString('zh-CN',{hour12:false})}}</span>
    </footer>

    <div v-if="selectedControl" class="hmi-modal-backdrop" @click.self="selectedControl=null">
      <section class="hmi-modal twin-control-modal"><header><div><span>核心控制</span><strong>{{selectedControl.displayName}}</strong></div><button class="modal-close" @click="selectedControl=null">×</button></header><label class="form-field"><span>操作原因</span><textarea v-model="controlReason" rows="3" maxlength="500" placeholder="填写本次操作原因"></textarea></label><div v-if="controlError" class="inline-alert" data-state="bad">{{controlError}}</div><footer><button class="btn-secondary" @click="selectedControl=null">取消</button><button class="btn-primary" :disabled="controlBusy" @click="prepareControl">确认信息</button></footer></section>
    </div>
    <CommandPreviewDialog v-if="pendingPreview" :preview="pendingPreview" :busy="controlBusy" :error="previewError" @confirm="confirmPreview" @cancel="pendingPreview=null"/>
  </div>
</template>

