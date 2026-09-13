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
}>(), {
  value: null,
  unit: '',
  thresholds: () => ({}),
  tone: 'info',
  detail: ''
});
const ratio = computed(() => props.value === null || props.value === undefined ? 0 : clampGauge(props.value, props.min, props.max));
const markerStyle = computed(() => ({ left: `${ratio.value * 100}%` }));
const valueStyle = computed(() => ({ width: `${ratio.value * 100}%` }));
const bands = computed(() => {
  const result:Array<{tone:'warning'|'alarm';left:string;width:string}> = [];
  const add=(start:number,end:number,tone:'warning'|'alarm')=>{
    const a=Math.max(0,Math.min(1,start));const b=Math.max(0,Math.min(1,end));
    if(b-a<0.002)return;result.push({tone,left:`${a*100}%`,width:`${(b-a)*100}%`});
  };
  const wl=props.thresholds.warningLow===undefined?undefined:clampGauge(props.thresholds.warningLow,props.min,props.max);
  const wh=props.thresholds.warningHigh===undefined?undefined:clampGauge(props.thresholds.warningHigh,props.min,props.max);
  const al=props.thresholds.alarmLow===undefined?undefined:clampGauge(props.thresholds.alarmLow,props.min,props.max);
  const ah=props.thresholds.alarmHigh===undefined?undefined:clampGauge(props.thresholds.alarmHigh,props.min,props.max);
  if(al!==undefined)add(0,al,'alarm');if(wl!==undefined)add(al??0,wl,'warning');if(wh!==undefined)add(wh,ah??1,'warning');if(ah!==undefined)add(ah,1,'alarm');
  return result;
});
const marks = computed(() => {
  const entries: Array<{value:number;tone:'warning'|'alarm'}> = [];
  const push = (value:number|undefined,tone:'warning'|'alarm') => {
    if (value === undefined || value < props.min || value > props.max) return;
    entries.push({ value, tone });
  };
  push(props.thresholds.warningLow,'warning');push(props.thresholds.warningHigh,'warning');push(props.thresholds.alarmLow,'alarm');push(props.thresholds.alarmHigh,'alarm');
  return entries.map((entry) => ({ tone:entry.tone, left:`${clampGauge(entry.value, props.min, props.max)*100}%` }));
});
function edge(value:number){ return Math.abs(value)>=100?Math.round(value).toString():value.toFixed(Math.abs(value)<10?1:0).replace(/\.0$/,''); }
</script>

<template>
  <div class="industrial-linear-gauge" :data-tone="tone">
    <div class="linear-gauge-head"><span>{{label}}</span><div><strong>{{displayValue}}</strong><em v-if="unit">{{unit}}</em></div></div>
    <div class="linear-gauge-track">
      <i v-for="(band,index) in bands" :key="`band-${index}`" class="linear-gauge-band" :data-tone="band.tone" :style="{left:band.left,width:band.width}"></i>
      <i class="linear-gauge-value" :style="valueStyle"></i>
      <i v-for="(mark,index) in marks" :key="index" class="linear-gauge-threshold" :data-tone="mark.tone" :style="{left:mark.left}"></i>
      <b v-if="value !== null && value !== undefined" class="linear-gauge-marker" :style="markerStyle"></b>
    </div>
    <div class="linear-gauge-scale"><span>{{edge(min)}}</span><small>{{detail || '实时监测'}}</small><span>{{edge(max)}}</span></div>
  </div>
</template>
