<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import { useRoute } from 'vue-router';
import { qualityLabel, type TelemetryHistoryResponse } from '@smart-factory/domain';
import { useSiteStore } from '../stores/site';
import PageHeader from '../components/PageHeader.vue';
import RealtimeTrendChart from '../components/RealtimeTrendChart.vue';
import type { TrendSeries } from '../trend-types';
import { formatTelemetryText } from '../utils/industrial-format';
import { trendColor } from '../utils/scenario-colors';

const store = useSiteStore();
const route = useRoute();
const deviceId = ref(String(route.query.deviceId ?? store.dashboard.devices[0]?.id ?? ''));
const pointId = ref(String(route.query.pointId ?? ''));
const rangeMinutes = ref(60);
const initialRouteFrom = Number(route.query.from);
const initialRouteTo = Number(route.query.to);
let useInitialRouteWindow = Number.isFinite(initialRouteFrom) && Number.isFinite(initialRouteTo) && initialRouteTo >= initialRouteFrom;
if (useInitialRouteWindow) rangeMinutes.value = Math.max(1, Math.round((initialRouteTo - initialRouteFrom) / 60_000));
const loading = ref(false);
const error = ref<string|null>(null);
const result = ref<TelemetryHistoryResponse>({ enabled:false, items:[], totalMatched:0, limit:2000, from:0, to:Date.now(), truncated:false });

const points = computed(() => store.dashboard.telemetry.filter((p) => p.deviceId === deviceId.value));
watch(deviceId, () => {
  if (!points.value.some((p) => p.pointId === pointId.value)) pointId.value = points.value[0]?.pointId ?? '';
});

const series = computed<TrendSeries[]>(() => {
  const selected = result.value.items.filter((p) => p.deviceId === deviceId.value && (!pointId.value || p.pointId === pointId.value));
  const numeric = selected.filter((p) => typeof p.value === 'number');
  if (!numeric.length) return [];
  const first = numeric[0];
  return [{
    key: `${first.deviceId}:${first.pointId}`,
    label: first.label,
    unit: first.unit,
    color: trendColor(store.dashboard.devices.find((device) => device.id === first.deviceId)?.scenario, first.pointId),
    samples: numeric.map((p, index) => ({
      ts: p.sampleTs,
      value: p.value as number,
      breakBefore: index > 0 && p.sampleTs - numeric[index - 1].sampleTs > Math.max(5_000, rangeMinutes.value * 60_000 / 100)
    }))
  }];
});

async function query(): Promise<void> {
  if (!deviceId.value || !pointId.value) return;
  loading.value = true; error.value = null;
  const to = useInitialRouteWindow ? initialRouteTo : Date.now();
  const from = useInitialRouteWindow ? initialRouteFrom : to - rangeMinutes.value * 60_000;
  // Route from/to are only an initial navigation context. Once the operator
  // runs a query, the visible time-range control becomes authoritative.
  useInitialRouteWindow = false;
  try {
    result.value = await store.queryTelemetryHistory({ deviceId:deviceId.value, pointId:pointId.value, from, to, limit:5000 });
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : String(cause);
  } finally { loading.value = false; }
}

function exportCsv(): void {
  if (!result.value.items.length) return;
  const header = ['deviceId','pointId','label','value','unit','quality','sampleTs','receiveTs','seq'];
  const escape = (v:unknown) => `"${String(v ?? '').replaceAll('"','""')}"`;
  const lines = [header.join(','), ...result.value.items.map((p) => header.map((k) => escape((p as unknown as Record<string,unknown>)[k])).join(','))];
  const blob = new Blob(['\uFEFF'+lines.join('\n')], { type:'text/csv;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url; a.download = `history-${deviceId.value}-${pointId.value}-${Date.now()}.csv`; a.click();
  URL.revokeObjectURL(url);
}

onMounted(() => {
  if (!pointId.value) pointId.value = points.value[0]?.pointId ?? '';
  void query();
});
</script>

<template>
<section class="operator-page" :data-scenario="store.dashboard.devices.find((device) => device.id === deviceId)?.scenario">
  <PageHeader title="历史趋势" eyebrow="设备与生产" description="按设备、测点和时间范围查询历史数据与趋势。"/>
  <div class="toolbar hmi-toolbar history-toolbar">
    <label class="select-field"><span>设备</span><select v-model="deviceId"><option v-for="d in store.dashboard.devices" :key="d.id" :value="d.id">{{ d.name }} · {{ d.id }}</option></select></label>
    <label class="select-field"><span>测点</span><select v-model="pointId"><option v-for="p in points" :key="p.pointId" :value="p.pointId">{{ p.label }} · {{ p.pointId }}</option></select></label>
    <label class="select-field"><span>时间范围</span><select v-model.number="rangeMinutes"><option :value="15">15 分钟</option><option :value="60">1 小时</option><option :value="360">6 小时</option><option :value="1440">24 小时</option></select></label>
    <button class="btn-primary" type="button" :disabled="loading||!pointId" @click="query">{{ loading?'查询中':'查询' }}</button>
    <button class="btn-secondary" type="button" :disabled="!result.items.length" @click="exportCsv">导出当前结果</button>
  </div>

  <div v-if="error" class="inline-alert" data-state="bad">{{ error }}</div>
  <div v-else-if="result.truncated" class="inline-alert" data-state="warning">查询结果已达到返回上限；当前导出仅包含本页已返回的数据，不代表完整时间范围。</div>
  <div v-else-if="!result.enabled" class="inline-alert" data-state="warning">当前没有可查询的历史数据。</div>

  <section class="hmi-panel history-trend-panel">
    <div class="hmi-panel-head"><div><span>历史数据</span><h2>历史趋势</h2></div><small>{{ result.totalMatched }} 条 · {{ result.truncated?'结果已截断':'查询完整' }}</small></div>
    <RealtimeTrendChart :series="series" :height="360" />
  </section>

  <section class="hmi-panel history-table-panel">
    <div class="hmi-panel-head"><div><span>数据明细</span><h2>历史记录</h2></div><small>最近显示 100 条</small></div>
    <div class="table-card"><table><thead><tr><th>采样时间</th><th>测点</th><th>数值</th><th>质量</th><th>序号</th></tr></thead><tbody>
      <tr v-for="p in result.items.slice(-100).reverse()" :key="`${p.deviceId}:${p.pointId}:${p.sampleTs}:${p.seq}`"><td class="table-time">{{new Date(p.sampleTs).toLocaleString('zh-CN',{hour12:false})}}</td><td><strong class="table-primary-cell">{{p.label}}</strong><small class="table-sub">{{p.pointId}}</small></td><td class="numeric-cell">{{formatTelemetryText(p)}}</td><td><span class="state-text" :data-state="p.quality.toLowerCase()">{{qualityLabel[p.quality]}}</span></td><td class="numeric-cell">{{p.seq}}</td></tr>
      <tr v-if="!result.items.length"><td colspan="5"><div class="table-empty">暂无历史数据</div></td></tr>
    </tbody></table></div>
  </section>
</section>
</template>
