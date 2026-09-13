<script setup lang="ts">
import { computed } from 'vue';
import {
  deviceProtocol,
  deviceStatusLabel,
  qualityLabel,
  severityLabel,
  type TelemetryPoint
} from '@smart-factory/domain';
import { useSiteStore } from '../stores/site';
import PageHeader from '../components/PageHeader.vue';
import PlantOverview from '../components/PlantOverview.vue';
import StatusPill from '../components/StatusPill.vue';
import RealtimeTrendChart, { type TrendSeries } from '../components/RealtimeTrendChart.vue';
import { useRealtimeTrendHistory } from '../composables/useRealtimeTrendHistory';
import { formatDurationSeconds, formatInteger, formatLatency, formatTelemetryText, publicSiteLabel } from '../utils/industrial-format';
import { trendColor } from '../utils/scenario-colors';

const store = useSiteStore();
const activeAlarms = computed(() => [...store.dashboard.alarms].filter((alarm) => alarm.state !== 'CLEARED').sort((a,b) => {
  const rank = (severity:string) => severity === 'CRITICAL' ? 3 : severity === 'WARNING' ? 2 : 1;
  return rank(b.severity) - rank(a.severity) || b.raisedAt - a.raisedAt;
}));
const criticalAlarms = computed(() => activeAlarms.value.filter((alarm) => alarm.severity === 'CRITICAL').length);
const warningAlarms = computed(() => activeAlarms.value.filter((alarm) => alarm.severity === 'WARNING').length);
const abnormalDevices = computed(() => store.dashboard.devices.filter((device) => device.status !== 'ONLINE'));
const keyTelemetry = computed(() => [...store.dashboard.telemetry].sort((a,b) => {
  const rank = (point:TelemetryPoint) => {
    const scenario = store.dashboard.devices.find((device) => device.id === point.deviceId)?.scenario;
    return scenario === 'hazardous_gas' ? 0 : scenario === 'temperature_humidity' ? 1 : scenario === 'agv_obstacle' ? 2 : 3;
  };
  return rank(a) - rank(b);
}).slice(0,8));
const numericTelemetry = computed(() => keyTelemetry.value.filter((point): point is TelemetryPoint & { value:number } => typeof point.value === 'number'));
const { history } = useRealtimeTrendHistory(numericTelemetry,{ maxSamples:180, gapMs:5000 });
const trendCandidates = computed(() => {
  const gas = numericTelemetry.value.filter((point) => store.dashboard.devices.find((device) => device.id === point.deviceId)?.scenario === 'hazardous_gas');
  const base = gas.length ? gas : numericTelemetry.value;
  return base.slice(0,3);
});
const trendSeries = computed<TrendSeries[]>(() => trendCandidates.value.map((point) => ({
  key:`${point.deviceId}:${point.pointId}`,
  label:point.label,
  unit:point.unit,
  color:trendColor(store.dashboard.devices.find((device) => device.id === point.deviceId)?.scenario, point.pointId),
  samples:history.value[`${point.deviceId}:${point.pointId}`] ?? []
})));
const communicationSummary = computed(() => ({
  total:store.dashboard.communication.length,
  degraded:store.dashboard.communication.filter((item) => item.quality !== 'GOOD').length,
  errors:store.dashboard.communication.reduce((sum,item) => sum + (item.errorCount??0),0),
  avgP95:(()=>{const values=store.dashboard.communication.map(item=>item.latencyMsP95).filter((v):v is number=>typeof v==='number');return values.length?values.reduce((a,b)=>a+b,0)/values.length:undefined;})()
}));
const overallState = computed(() => criticalAlarms.value ? 'ALARM' : activeAlarms.value.length || abnormalDevices.value.length || communicationSummary.value.degraded ? 'ATTENTION' : 'NORMAL');
const overallLabel = computed(() => overallState.value === 'ALARM' ? '存在严重报警' : overallState.value === 'ATTENTION' ? '需要关注' : '运行正常');
const overallTone = computed<'good'|'warn'|'bad'>(() => overallState.value === 'ALARM' ? 'bad' : overallState.value === 'ATTENTION' ? 'warn' : 'good');

function alarmTone(severity:string): 'info'|'warn'|'bad' { return severity === 'CRITICAL' ? 'bad' : severity === 'WARNING' ? 'warn' : 'info'; }
function scenarioState(s:{activeAlarmCount:number;onlineCount:number;deviceCount:number}) { return s.activeAlarmCount ? 'warning' : s.onlineCount < s.deviceCount ? 'offline' : 'normal'; }
function pointText(point:TelemetryPoint): string { return formatTelemetryText(point); }
</script>

<template>
<section class="operator-page overview-operator-page">
  <PageHeader
    title="智慧工厂安全监测控制平台"
    :eyebrow="publicSiteLabel(store.dashboard.siteName)"
    description="集中查看安全状态、设备运行、工业通信和实时数据。"
  >
    <template #actions><StatusPill :tone="overallTone" :label="overallLabel" /></template>
  </PageHeader>

  <div class="overview-status-band" :data-state="overallState.toLowerCase()">
    <div class="overview-status-primary"><span>工厂状态</span><strong>{{overallLabel}}</strong></div>
    <div><span>设备在线</span><strong>{{store.onlineDevices}} / {{store.dashboard.devices.length}}</strong></div>
    <div><span>活动报警</span><strong :class="activeAlarms.length ? 'text-warning' : ''">{{activeAlarms.length}}</strong><small v-if="criticalAlarms">严重 {{criticalAlarms}}</small></div>
    <div><span>工业通信</span><strong>{{communicationSummary.degraded ? `${communicationSummary.degraded} 条异常` : '正常'}}</strong><small>P95 {{formatLatency(communicationSummary.avgP95)}}</small></div>
    <div><span>数据刷新</span><strong>{{formatDurationSeconds(store.dashboard.generatedAt)}}</strong><small>{{store.dataOriginLabel}}</small></div>
  </div>

  <div class="overview-control-grid" :class="{ 'has-priority-alarm': activeAlarms.length > 0 }">
    <section class="hmi-panel plant-overview-panel overview-main-plant">
      <div class="hmi-panel-head"><div><span>现场</span><h2>工业现场总览</h2></div><small>{{store.dashboard.devices.length}} 台设备</small></div>
      <PlantOverview :devices="store.dashboard.devices" :telemetry="store.dashboard.telemetry" />
    </section>

    <section class="hmi-panel overview-attention-panel" :data-state="activeAlarms.length ? 'active' : 'quiet'">
      <div class="hmi-panel-head"><div><span>安全</span><h2>{{activeAlarms.length ? '需要关注' : '当前安全状态'}}</h2></div><RouterLink to="/alarms">报警中心</RouterLink></div>
      <div v-if="activeAlarms.length" class="overview-priority-list">
        <article v-for="alarm in activeAlarms.slice(0,5)" :key="alarm.id" class="overview-priority-event" :data-severity="alarm.severity.toLowerCase()">
          <div><StatusPill :tone="alarmTone(alarm.severity)" :label="severityLabel[alarm.severity]"/><time>{{new Date(alarm.raisedAt).toLocaleTimeString('zh-CN',{hour12:false})}}</time></div>
          <strong>{{alarm.title}}</strong>
          <span>{{store.dashboard.devices.find((device) => device.id === alarm.deviceId)?.name ?? alarm.deviceId}}</span>
          <p>{{alarm.message}}</p>
        </article>
      </div>
      <div v-else class="overview-quiet-safety">
        <span class="overview-quiet-symbol">✓</span>
        <div><strong>当前无活动报警</strong><p>设备与安全监测未发现需要立即处置的异常。</p></div>
      </div>
      <div v-if="abnormalDevices.length" class="overview-abnormal-devices">
        <strong>异常设备</strong>
        <RouterLink v-for="device in abnormalDevices.slice(0,4)" :key="device.id" :to="`/devices/${device.id}`">
          <span>{{device.name}}</span><b>{{deviceStatusLabel[device.status]}}</b>
        </RouterLink>
      </div>
    </section>
  </div>

  <div class="overview-monitoring-grid">
    <section class="hmi-panel overview-scenario-panel">
      <div class="hmi-panel-head"><div><span>安全监测</span><h2>五类现场监测</h2></div><RouterLink to="/scenarios">查看全部</RouterLink></div>
      <div class="overview-scenario-list">
        <RouterLink v-for="scenario in store.dashboard.scenarios" :key="scenario.type" :to="`/scenarios/${scenario.type}`" :data-scenario="scenario.type" :data-state="scenarioState(scenario)">
          <span class="hmi-state-indicator"><i></i></span>
          <div><strong>{{scenario.label}}</strong><small>在线 {{scenario.onlineCount}} / {{scenario.deviceCount}}</small></div>
          <b v-if="scenario.activeAlarmCount">{{scenario.activeAlarmCount}} 报警</b><b v-else>正常</b>
        </RouterLink>
      </div>
    </section>

    <section class="hmi-panel overview-trend-panel">
      <div class="hmi-panel-head"><div><span>实时变化</span><h2>{{trendCandidates.length ? '关键实时趋势' : '实时趋势'}}</h2></div><RouterLink to="/monitor">实时监控</RouterLink></div>
      <RealtimeTrendChart :series="trendSeries" :height="270" />
    </section>

    <section class="hmi-panel overview-values-panel">
      <div class="hmi-panel-head"><div><span>实时数据</span><h2>关键参数</h2></div><small>{{keyTelemetry.length}} 个测点</small></div>
      <div class="overview-value-list">
        <div v-for="point in keyTelemetry" :key="`${point.deviceId}:${point.pointId}`" :data-scenario="store.dashboard.devices.find((device) => device.id === point.deviceId)?.scenario" :data-quality="point.quality.toLowerCase()">
          <div><strong>{{point.label}}</strong><small>{{store.dashboard.devices.find((device) => device.id === point.deviceId)?.name}}</small></div>
          <b>{{pointText(point)}}</b>
          <span>{{qualityLabel[point.quality]}}</span>
        </div>
      </div>
    </section>
  </div>

  <section class="hmi-panel overview-communication-panel">
    <div class="hmi-panel-head"><div><span>工业通信</span><h2>通信链路状态</h2></div><RouterLink to="/communication">通信状态</RouterLink></div>
    <div class="communication-strip">
      <div v-for="item in store.dashboard.communication" :key="item.deviceId" class="communication-cell" :data-quality="item.quality.toLowerCase()">
        <div class="communication-cell-head"><span class="hmi-state-indicator"><i></i></span><strong>{{store.dashboard.devices.find((device) => device.id === item.deviceId)?.name ?? item.deviceId}}</strong></div>
        <span>{{store.dashboard.devices.find((device) => device.id === item.deviceId) ? deviceProtocol(store.dashboard.devices.find((device) => device.id === item.deviceId)!) : '—'}}</span>
        <b>P95 {{formatLatency(item.latencyMsP95)}}</b>
        <small>错误 {{formatInteger(item.errorCount)}} · 更新 {{formatDurationSeconds(item.lastSeen)}}</small>
      </div>
    </div>
  </section>
</section>
</template>
