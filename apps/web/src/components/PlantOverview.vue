<script setup lang="ts">
import { computed } from 'vue';
import { deviceStatusLabel, type Device, type TelemetryPoint } from '@smart-factory/domain';
import AppIcon from './AppIcon.vue';
import { formatTelemetryText } from '../utils/industrial-format';

const props = defineProps<{ devices: Device[]; telemetry: TelemetryPoint[] }>();

const scenarioUi: Record<string,{icon:string;short:string}>={
  temperature_humidity:{icon:'environment',short:'环境监测'},
  pir_lighting:{icon:'lighting',short:'感应照明'},
  hazardous_gas:{icon:'gas',short:'危气监测'},
  agv_obstacle:{icon:'agv',short:'AGV 避障'},
  goods_counting:{icon:'count',short:'货物计数'}
};

const areas=computed(()=>{
  const byArea=new Map<string,Device[]>();
  for(const device of props.devices){
    const area=device.location?.trim()||'未分区';
    byArea.set(area,[...(byArea.get(area)??[]),device]);
  }
  return [...byArea.entries()].sort(([a],[b])=>a.localeCompare(b,'zh-CN')).map(([name,devices])=>({
    name,
    devices:devices.map(device=>{
      const points=props.telemetry.filter((p)=>p.deviceId===device.id);
      const keyPoint=points.find((p)=>typeof p.value==='number')??points[0];
      return {device,keyPoint,ui:scenarioUi[device.scenario]??{icon:'devices',short:'现场设备'}};
    })
  }));
});

function state(device: Device): 'normal'|'warning'|'offline' {
  if (device.status === 'OFFLINE') return 'offline';
  if (device.status === 'DEGRADED') return 'warning';
  return 'normal';
}
function format(point?: TelemetryPoint): string { return formatTelemetryText(point); }
</script>

<template>
  <div class="plant-overview" aria-label="工业现场逻辑总览">
    <div class="plant-map-caption">
      <span>现场区域</span>
      <small>按区域显示现场设备及关键状态</small>
    </div>
    <div class="plant-area-grid">
      <section v-for="area in areas" :key="area.name" class="plant-area">
        <header><strong>{{area.name}}</strong><span>{{area.devices.length}} 台设备</span></header>
        <div class="plant-area-devices">
          <RouterLink
            v-for="block in area.devices"
            :key="block.device.id"
            :to="`/devices/${block.device.id}`"
            class="plant-object"
            :data-scenario="block.device.scenario"
            :data-state="state(block.device)"
          >
            <div class="plant-object-head">
              <span class="plant-object-symbol"><AppIcon :name="block.ui.icon" :size="18" /></span>
              <span class="plant-object-state"><i></i>{{ deviceStatusLabel[block.device.status] }}</span>
            </div>
            <strong>{{ block.ui.short }}</strong>
            <span class="plant-object-name">{{ block.device.name }}</span>
            <div class="plant-object-value"><span>{{ block.keyPoint?.label ?? '状态' }}</span><b>{{ format(block.keyPoint) }}</b></div>
            <small>{{block.device.id}}</small>
          </RouterLink>
        </div>
      </section>
      <div v-if="!areas.length" class="plant-overview-empty">暂无现场设备</div>
    </div>
  </div>
</template>
