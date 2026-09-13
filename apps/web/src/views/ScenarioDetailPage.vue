<script setup lang="ts">
import { computed } from 'vue';
import { useRoute } from 'vue-router';
import {
  deviceProtocol,
  scenarioDescriptions,
  scenarioLabels,
  severityLabel,
  type ScenarioType,
  type TelemetryPoint
} from '@smart-factory/domain';
import { useSiteStore } from '../stores/site';
import PageHeader from '../components/PageHeader.vue';
import StatusPill from '../components/StatusPill.vue';
import RealtimeTrendChart, { type TrendSeries } from '../components/RealtimeTrendChart.vue';
import ScenarioProcessPanel from '../components/ScenarioProcessPanel.vue';
import { useRealtimeTrendHistory } from '../composables/useRealtimeTrendHistory';
import { formatInteger, formatLatency } from '../utils/industrial-format';
import { alarmStateLabel } from '../utils/operator-labels';
import { trendColor } from '../utils/scenario-colors';

const route = useRoute();
const store = useSiteStore();
const type = computed(() => route.params.type as ScenarioType);
const devices = computed(() => store.dashboard.devices.filter((device) => device.scenario === type.value));
const telemetry = computed(() => store.dashboard.telemetry.filter((point) => devices.value.some((device) => device.id === point.deviceId)));
const alarms = computed(() => store.dashboard.alarms.filter((alarm) => devices.value.some((device) => device.id === alarm.deviceId)).sort((a,b) => b.raisedAt - a.raisedAt));
const comm = computed(() => store.dashboard.communication.filter((item) => devices.value.some((device) => device.id === item.deviceId)));
const numeric = computed(() => telemetry.value.filter((point): point is TelemetryPoint & { value:number } => typeof point.value === 'number'));
const grouped = computed(() => {
  const map = new Map<string,TelemetryPoint[]>();
  for (const point of numeric.value) {
    const key = point.unit ?? 'value';
    map.set(key, [...(map.get(key) ?? []), point]);
  }
  return [...map.entries()].slice(0, 2);
});
const { history } = useRealtimeTrendHistory(numeric, { maxSamples:180, gapMs:5000 });
const primaryDeviceId = computed(() => devices.value[0]?.id ?? '');
const primaryPointId = computed(() => telemetry.value.find((point) => typeof point.value === 'number')?.pointId ?? telemetry.value[0]?.pointId ?? '');

function series(points: TelemetryPoint[]): TrendSeries[] {
  return points.slice(0,3).map((point) => ({
    key:`${point.deviceId}:${point.pointId}`,
    label:point.label,
    unit:point.unit,
    color:trendColor(type.value, point.pointId),
    samples:history.value[`${point.deviceId}:${point.pointId}`] ?? []
  }));
}
function severityTone(severity:string): 'info'|'warn'|'bad' { return severity === 'CRITICAL' ? 'bad' : severity === 'WARNING' ? 'warn' : 'info'; }
</script>

<template>
<section class="operator-page scenario-business-page" :data-scenario="type">
  <PageHeader
    :title="scenarioLabels[type] ?? type"
    eyebrow="安全监测"
    back-to="/scenarios"
    back-label="返回安全监测"
    :description="scenarioDescriptions[type] ?? '现场安全监测场景'"
  >
    <template #actions>
      <RouterLink
        v-if="primaryDeviceId && primaryPointId"
        class="btn-secondary link-button"
        :to="{ path:'/history', query:{ deviceId:primaryDeviceId, pointId:primaryPointId } }"
      >查看历史</RouterLink>
    </template>
  </PageHeader>

  <ScenarioProcessPanel
    :type="type"
    :devices="devices"
    :telemetry="telemetry"
    :communication="comm"
    :alarms="alarms"
  />

  <section class="hmi-panel scenario-trend-panel">
    <div class="hmi-panel-head">
      <div><span>趋势</span><h2>实时变化</h2></div>
      <small>{{ grouped.length ? '仅在新数据到达时更新' : '当前无数值型测点' }}</small>
    </div>
    <div v-if="grouped.length" class="scenario-trend-grid">
      <div v-for="([unit,points],index) in grouped" :key="unit" class="scenario-trend-item">
        <RealtimeTrendChart :series="series(points)" :height="index===0 ? 300 : 240" />
      </div>
    </div>
    <div v-else class="card-empty">当前场景没有可绘制的实时数值趋势</div>
  </section>

  <div class="scenario-bottom-grid">
    <section class="hmi-panel scenario-communication-panel">
      <div class="hmi-panel-head"><div><span>通信</span><h2>通信状态</h2></div><RouterLink to="/communication">全部链路</RouterLink></div>
      <div class="comm-diagnostic-list comm-diagnostic-list-wide">
        <div v-for="item in comm" :key="item.deviceId" :data-quality="item.quality.toLowerCase()">
          <span class="hmi-state-indicator"><i></i></span>
          <div>
            <strong>{{devices.find((device) => device.id === item.deviceId)?.name ?? item.deviceId}}</strong>
            <small>{{devices.find((device) => device.id === item.deviceId) ? deviceProtocol(devices.find((device) => device.id === item.deviceId)!) : '—'}}</small>
          </div>
          <div><b>P95 {{formatLatency(item.latencyMsP95)}}</b><span>错误 {{formatInteger(item.errorCount)}}</span></div>
        </div>
        <div v-if="!comm.length" class="card-empty">当前场景暂无通信状态</div>
      </div>
    </section>

    <section class="hmi-panel scenario-alarm-panel">
      <div class="hmi-panel-head"><div><span>安全事件</span><h2>相关报警</h2></div><RouterLink to="/alarms">报警中心</RouterLink></div>
      <div class="scenario-alarm-list">
        <div v-for="alarm in alarms.slice(0,8)" :key="alarm.id" class="scenario-alarm-row" :data-severity="alarm.severity.toLowerCase()">
          <StatusPill :tone="severityTone(alarm.severity)" :label="severityLabel[alarm.severity]"/>
          <div><strong>{{alarm.title}}</strong><small>{{new Date(alarm.raisedAt).toLocaleString('zh-CN',{hour12:false})}} · {{alarmStateLabel[alarm.state]}}</small></div>
        </div>
        <div v-if="!alarms.length" class="quiet-inline-state"><span>✓</span><strong>当前无相关报警</strong></div>
      </div>
    </section>
  </div>
</section>
</template>
