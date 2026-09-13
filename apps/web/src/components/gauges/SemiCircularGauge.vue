<script setup lang="ts">
import { computed } from 'vue';
import { clampGauge } from './gauge.utils';
import type { GaugeThresholds, GaugeTone } from './gauge.types';

const props = withDefaults(defineProps<{
  label: string;
  value?: number | null;
  displayValue: string;
  unit?: string;
  min: number;
  max: number;
  thresholds?: GaugeThresholds;
  tone?: GaugeTone;
  detail?: string;
  compact?: boolean;
}>(), {
  value: null,
  unit: '',
  thresholds: () => ({}),
  tone: 'info',
  detail: '',
  compact: false
});

const CENTER_X = 90;
const CENTER_Y = 82;
const RADIUS = 63;
const valueRatio = computed(() => props.value === null || props.value === undefined ? 0 : clampGauge(props.value, props.min, props.max));
const pointer = computed(() => {
  const angle = Math.PI + valueRatio.value * Math.PI;
  const radius = 48;
  return {
    x: CENTER_X + radius * Math.cos(angle),
    y: CENTER_Y + radius * Math.sin(angle)
  };
});
const valueArc = computed(() => arcPath(0, valueRatio.value, RADIUS));
const ticks = computed(() => Array.from({ length: 11 }, (_, index) => {
  const ratio = index / 10;
  const angle = Math.PI + ratio * Math.PI;
  return {
    x1: CENTER_X + 55 * Math.cos(angle),
    y1: CENTER_Y + 55 * Math.sin(angle),
    x2: CENTER_X + 61 * Math.cos(angle),
    y2: CENTER_Y + 61 * Math.sin(angle)
  };
}));
const thresholdBands = computed(() => {
  const bands: Array<{ start:number; end:number; tone:'warning'|'alarm'; path:string }> = [];
  const add = (start:number, end:number, tone:'warning'|'alarm') => {
    const a = Math.max(0, Math.min(1, start));
    const b = Math.max(0, Math.min(1, end));
    if (b - a < 0.002) return;
    bands.push({ start:a, end:b, tone, path:arcPath(a,b,69) });
  };
  const wl = props.thresholds.warningLow === undefined ? undefined : clampGauge(props.thresholds.warningLow, props.min, props.max);
  const wh = props.thresholds.warningHigh === undefined ? undefined : clampGauge(props.thresholds.warningHigh, props.min, props.max);
  const al = props.thresholds.alarmLow === undefined ? undefined : clampGauge(props.thresholds.alarmLow, props.min, props.max);
  const ah = props.thresholds.alarmHigh === undefined ? undefined : clampGauge(props.thresholds.alarmHigh, props.min, props.max);
  if (al !== undefined) add(0, al, 'alarm');
  if (wl !== undefined) add(al ?? 0, wl, 'warning');
  if (wh !== undefined) add(wh, ah ?? 1, 'warning');
  if (ah !== undefined) add(ah, 1, 'alarm');
  return bands;
});
const thresholdMarks = computed(() => {
  const entries: Array<{ value:number; tone:'warning'|'alarm' }> = [];
  const push = (value:number|undefined, tone:'warning'|'alarm') => {
    if (value === undefined || value < props.min || value > props.max) return;
    entries.push({ value, tone });
  };
  push(props.thresholds.warningLow, 'warning');
  push(props.thresholds.warningHigh, 'warning');
  push(props.thresholds.alarmLow, 'alarm');
  push(props.thresholds.alarmHigh, 'alarm');
  return entries.map((entry) => {
    const ratio = clampGauge(entry.value, props.min, props.max);
    const angle = Math.PI + ratio * Math.PI;
    return {
      tone: entry.tone,
      x1: CENTER_X + 52 * Math.cos(angle),
      y1: CENTER_Y + 52 * Math.sin(angle),
      x2: CENTER_X + 68 * Math.cos(angle),
      y2: CENTER_Y + 68 * Math.sin(angle)
    };
  });
});

function point(ratio:number, radius:number) {
  const angle = Math.PI + ratio * Math.PI;
  return { x: CENTER_X + radius * Math.cos(angle), y: CENTER_Y + radius * Math.sin(angle) };
}
function arcPath(start:number, end:number, radius:number) {
  if (end <= start) return '';
  const a = point(start, radius);
  const b = point(end, radius);
  return `M ${a.x.toFixed(2)} ${a.y.toFixed(2)} A ${radius} ${radius} 0 0 1 ${b.x.toFixed(2)} ${b.y.toFixed(2)}`;
}
function edgeLabel(value:number) {
  const magnitude = Math.abs(value);
  if (magnitude >= 100) return Math.round(value).toString();
  if (magnitude >= 10) return value.toFixed(0);
  return value.toFixed(1).replace(/\.0$/, '');
}
</script>

<template>
  <div class="industrial-semi-gauge" :class="{ 'is-compact': compact }" :data-tone="tone">
    <div class="industrial-gauge-title"><span>{{label}}</span><i></i></div>
    <svg viewBox="0 0 180 106" aria-hidden="true">
      <path class="gauge-scale-track" d="M 27 82 A 63 63 0 0 1 153 82"/>
      <path v-for="(band,index) in thresholdBands" :key="`band-${index}`" class="gauge-threshold-band" :data-tone="band.tone" :d="band.path"/>
      <path v-if="valueArc" class="gauge-value-arc" :d="valueArc"/>
      <g class="gauge-ticks">
        <line v-for="(tick,index) in ticks" :key="index" v-bind="tick"/>
      </g>
      <g class="gauge-thresholds">
        <line v-for="(mark,index) in thresholdMarks" :key="index" v-bind="mark" :data-tone="mark.tone"/>
      </g>
      <line v-if="value !== null && value !== undefined" class="gauge-pointer" :x1="CENTER_X" :y1="CENTER_Y" :x2="pointer.x" :y2="pointer.y"/>
      <circle class="gauge-hub" :cx="CENTER_X" :cy="CENTER_Y" r="4.4"/>
      <text class="gauge-min" x="24" y="98">{{edgeLabel(min)}}</text>
      <text class="gauge-max" x="156" y="98" text-anchor="end">{{edgeLabel(max)}}</text>
    </svg>
    <div class="industrial-gauge-reading"><strong>{{displayValue}}</strong><em v-if="unit">{{unit}}</em></div>
    <div class="industrial-gauge-foot"><span>{{detail || '实时监测'}}</span><b>{{tone==='alarm'?'报警':tone==='warning'?'预警':tone==='offline'?'无有效数据':tone==='normal'?'正常':'监测'}}</b></div>
  </div>
</template>
