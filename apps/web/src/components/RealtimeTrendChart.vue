<script setup lang="ts">
import { computed } from 'vue';
import type { TrendSample, TrendSeries } from '../trend-types';
import { formatNumber } from '../utils/industrial-format';
export type { TrendSample, TrendSeries } from '../trend-types';
const props = defineProps<{ series: TrendSeries[]; height?: number }>();

const width = 900;
const height = computed(() => props.height ?? 300);
const padding = { left: 52, right: 18, top: 18, bottom: 32 };
const plotWidth = computed(() => width - padding.left - padding.right);
const plotHeight = computed(() => height.value - padding.top - padding.bottom);

const allSamples = computed(() => props.series.flatMap((s) => s.samples));
const minTs = computed(() => allSamples.value.length ? Math.min(...allSamples.value.map((s) => s.ts)) : Date.now() - 60_000);
const maxTs = computed(() => allSamples.value.length ? Math.max(...allSamples.value.map((s) => s.ts)) : Date.now());
const rawMin = computed(() => allSamples.value.length ? Math.min(...allSamples.value.map((s) => s.value)) : 0);
const rawMax = computed(() => allSamples.value.length ? Math.max(...allSamples.value.map((s) => s.value)) : 1);
const range = computed(() => Math.max(1e-6, rawMax.value - rawMin.value));
const yMin = computed(() => rawMin.value - range.value * 0.12);
const yMax = computed(() => rawMax.value + range.value * 0.12);

function x(ts: number): number {
  const span = Math.max(1, maxTs.value - minTs.value);
  return padding.left + ((ts - minTs.value) / span) * plotWidth.value;
}
function y(value: number): number {
  return padding.top + (1 - ((value - yMin.value) / Math.max(1e-6, yMax.value - yMin.value))) * plotHeight.value;
}
function points(samples: TrendSample[]): string {
  return samples.map((s) => `${x(s.ts).toFixed(1)},${y(s.value).toFixed(1)}`).join(' ');
}
function segments(samples:TrendSample[]):TrendSample[][]{
  const result:TrendSample[][]=[];
  let current:TrendSample[]=[];
  for(const sample of samples){
    if(sample.breakBefore && current.length){ result.push(current); current=[]; }
    current.push(sample);
  }
  if(current.length) result.push(current);
  return result;
}
function timeLabel(ts: number): string {
  return new Date(ts).toLocaleTimeString('zh-CN', { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' });
}
const gridValues = computed(() => Array.from({ length: 5 }, (_, i) => yMax.value - ((yMax.value - yMin.value) * i) / 4));
</script>

<template>
  <div class="trend-chart" :style="{ '--trend-height': `${height}px` }">
    <div class="trend-legend">
      <div v-for="item in series" :key="item.key" class="trend-legend-item">
        <span class="trend-line-key" :style="{ background: item.color }"></span>
        <span>{{ item.label }}</span>
        <strong v-if="item.samples.length">{{ formatNumber(item.samples[item.samples.length - 1].value,{unit:item.unit,label:item.label}) }}{{ item.unit ? ` ${item.unit}` : '' }}</strong>
        <strong v-else>无实时样本</strong>
      </div>
    </div>
    <svg class="trend-svg" :viewBox="`0 0 ${width} ${height}`" role="img" aria-label="实时趋势曲线">
      <g class="trend-grid">
        <line v-for="(v, i) in gridValues" :key="`grid-${i}`" :x1="padding.left" :x2="width-padding.right" :y1="y(v)" :y2="y(v)" />
      </g>
      <g class="trend-axis-labels">
        <text v-for="(v, i) in gridValues" :key="`label-${i}`" x="46" :y="y(v)+4" text-anchor="end">{{ formatNumber(v,{decimals:1}) }}</text>
        <text :x="padding.left" :y="height-8">{{ timeLabel(minTs) }}</text>
        <text :x="width-padding.right" :y="height-8" text-anchor="end">{{ timeLabel(maxTs) }}</text>
      </g>
      <template v-for="item in series" :key="item.key">
        <polyline
          v-for="(segment,index) in segments(item.samples)"
          :key="`${item.key}:${index}`"
          class="trend-polyline"
          :stroke="item.color"
          :points="points(segment)"
        />
      </template>
      <line class="trend-now-line" :x1="width-padding.right" :x2="width-padding.right" :y1="padding.top" :y2="height-padding.bottom" />
    </svg>
    <div v-if="!allSamples.length || allSamples.length < 2" class="trend-empty">等待新的实时数据</div>
  </div>
</template>
