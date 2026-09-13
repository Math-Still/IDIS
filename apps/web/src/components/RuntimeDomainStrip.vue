<script setup lang="ts">
import { computed } from 'vue';
import { useSiteStore } from '../stores/site';

const store = useSiteStore();
const realtimeLabel = computed(() => {
  if (store.dataSource !== 'BACKEND') return '未接入';
  if (store.realtimeState === 'CONNECTED') return '正常';
  if (store.realtimeState === 'CONNECTING' || store.realtimeState === 'RECONNECTING') return '连接中';
  return '通信受限';
});
const realtimeState = computed(() => store.realtimeState === 'CONNECTED' ? 'normal' : ['CONNECTING','RECONNECTING'].includes(store.realtimeState) ? 'warning' : 'offline');
const businessOnline = computed(() => store.dataSource === 'BACKEND' && !store.error);
</script>

<template>
  <div class="runtime-domain-strip" aria-label="系统运行状态">
    <div class="runtime-domain-item">
      <span class="runtime-domain-label">实时数据</span>
      <span class="runtime-domain-state" :data-state="realtimeState"><i></i>{{ realtimeLabel }}</span>
    </div>
    <div class="runtime-domain-divider"></div>
    <div class="runtime-domain-item">
      <span class="runtime-domain-label">业务服务</span>
      <span class="runtime-domain-state" :data-state="businessOnline ? 'normal' : 'offline'"><i></i>{{ businessOnline ? '正常' : '状态未知' }}</span>
    </div>
  </div>
</template>
