<script setup lang="ts">
import { computed } from 'vue';
import { deviceProtocol, deviceStatusLabel, qualityLabel, type CommunicationHealth, type Device, type TelemetryPoint } from '@smart-factory/domain';
import StatusPill from './StatusPill.vue';
import { formatDurationSeconds, formatLatency, formatTelemetryText } from '../utils/industrial-format';
const props=defineProps<{device:Device;telemetry:TelemetryPoint[];communication?:CommunicationHealth}>();
const statusTone=computed<'good'|'warn'|'info'|'muted'>(()=>props.device.status==='ONLINE'?'good':props.device.status==='DEGRADED'?'warn':props.device.status==='MAINTENANCE'?'info':'muted');
const freshness=computed(()=>formatDurationSeconds(props.device.lastSeen));
function formatValue(point: TelemetryPoint): string { return formatTelemetryText(point); }
</script>
<template>
<article class="device-card" :data-scenario="device.scenario" :data-device-state="device.status.toLowerCase()">
  <div class="card-head">
    <div><span class="faceplate-kicker">DEVICE</span><h3>{{device.name}}</h3><p>{{device.location}}</p></div>
    <StatusPill :tone="statusTone" :label="deviceStatusLabel[device.status]"/>
  </div>
  <div v-if="telemetry.length" class="telemetry-grid">
    <div v-for="point in telemetry" :key="point.pointId" :data-quality="point.quality.toLowerCase()">
      <span>{{point.label}}</span><strong>{{formatValue(point)}}</strong><small>{{qualityLabel[point.quality]}}</small>
    </div>
  </div>
  <div v-else class="card-empty">NO DATA / 暂无测点数据</div>
  <div class="device-meta"><span>{{deviceProtocol(device)}}</span><span>{{device.osNode}}</span><span v-if="communication">P95 {{formatLatency(communication.latencyMsP95)}}</span><span>{{freshness}}</span></div>
  <RouterLink class="detail-link" :to="`/devices/${device.id}`">OPEN FACEPLATE</RouterLink>
</article>
</template>
