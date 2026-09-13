<script setup lang="ts">
import { computed } from 'vue';
import type { TelemetryPoint } from '@smart-factory/domain';
const props=defineProps<{point?:TelemetryPoint|null}>();
const origin=computed(()=>props.point?.provenance?.originKind??'UNKNOWN');
const originLabel=computed(()=>origin.value==='REAL_DEVICE'?'真实采集':origin.value==='SIMULATION'?'内置数据源':'来源未知');
const detail=computed(()=>{
  const p=props.point;
  if(!p)return '暂无数据';
  const reasons=p.qualityReasons?.length?p.qualityReasons.join(' · '):'状态正常';
  return `${originLabel.value} · ${p.quality} · ${reasons}`;
});
</script>
<template>
  <span class="source-quality-badge" :data-quality="point?.quality?.toLowerCase()??'unknown'" :title="detail">
    {{originLabel}} · {{point?.quality??'UNKNOWN'}}
  </span>
</template>
