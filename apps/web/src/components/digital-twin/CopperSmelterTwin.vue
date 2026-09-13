<script setup lang="ts">
import { computed, ref } from 'vue';
import type { Alarm, Device, TelemetryPoint } from '@smart-factory/domain';
import plantImage from '../../assets/digital-twin/copper-smelter-overview-2k.webp';
import plantImageUltrawide from '../../assets/digital-twin/copper-smelter-overview-ultrawide.webp';
import { TWIN_ZONES } from './twin.config';
import { buildTwinZoneRuntime, twinStatusLabel } from './twin.runtime';
import type { TwinZoneRuntime } from './twin.types';

const props = defineProps<{
  devices: Device[];
  telemetry: TelemetryPoint[];
  alarms: Alarm[];
  activeZoneId?: string;
}>();
const emit = defineEmits<{ selectZone:[zone:TwinZoneRuntime] }>();

const hoverZoneId = ref<string | null>(null);
const runtimeZones = computed(() => buildTwinZoneRuntime(TWIN_ZONES, props.devices, props.alarms, props.telemetry));
const runtimeMap = computed(() => new Map(runtimeZones.value.map((item) => [item.zone.id, item])));
const focusZone = computed(() => runtimeMap.value.get(hoverZoneId.value ?? props.activeZoneId ?? '') ?? runtimeZones.value[0]);

const HOTSPOTS: Record<string,{x:number;y:number;w:number;h:number;anchorX:number;anchorY:number;labelX:number;labelY:number}> = {
  'raw-material': { x: 8, y: 7, w: 24, h: 24, anchorX: 19, anchorY: 17, labelX: 18.5, labelY: 13.5 },
  smelting:       { x: 29, y: 10, w: 27, h: 27, anchorX: 40, anchorY: 18, labelX: 41, labelY: 14.5 },
  converting:     { x: 20, y: 31, w: 31, h: 28, anchorX: 33, anchorY: 39, labelX: 33, labelY: 40.5 },
  refining:       { x: 48, y: 43, w: 28, h: 24, anchorX: 63, anchorY: 52, labelX: 59, labelY: 52.5 },
  casting:        { x: 42, y: 64, w: 30, h: 24, anchorX: 54, anchorY: 70, labelX: 53, labelY: 68.5 },
  acid:           { x: 66, y: 10, w: 31, h: 38, anchorX: 79, anchorY: 23, labelX: 78.5, labelY: 22.5 },
  utilities:      { x: 2, y: 45, w: 28, h: 30, anchorX: 17, anchorY: 56, labelX: 17, labelY: 56 }
};

function hotspotFor(id:string){ return HOTSPOTS[id] ?? { x:45, y:45, w:10, h:10, anchorX:50, anchorY:50, labelX:50, labelY:50 }; }
function selectZone(item:TwinZoneRuntime){ emit('selectZone', item); }
function statusText(item?:TwinZoneRuntime){ return item?.devices.length ? twinStatusLabel(item.status) : '未绑定'; }
</script>

<template>
  <div class="copper-twin copper-twin-image" aria-label="铜冶炼厂数字孪生厂区视图">
    <div class="copper-twin-canvas">
      <picture class="copper-twin-picture">
        <source media="(min-width: 3000px) and (min-aspect-ratio: 28/9)" :srcset="plantImageUltrawide" />
        <img class="copper-twin-hero" :src="plantImage" alt="铜冶炼厂三维厂区数字孪生总览图" />
      </picture>
      <div class="copper-twin-image-shade" aria-hidden="true"></div>
      <div class="copper-twin-hotspots" aria-label="厂区工艺区域">
      <button
        v-for="item in runtimeZones"
        :key="item.zone.id"
        type="button"
        class="copper-twin-hotspot"
        :class="{ 'is-active': item.zone.id === activeZoneId, 'is-hover': item.zone.id === hoverZoneId }"
        :data-status="item.status"
        :style="{
          left:`${hotspotFor(item.zone.id).x}%`,
          top:`${hotspotFor(item.zone.id).y}%`,
          width:`${hotspotFor(item.zone.id).w}%`,
          height:`${hotspotFor(item.zone.id).h}%`
        }"
        :aria-label="`${item.zone.name}，${statusText(item)}`"
        @mouseenter="hoverZoneId=item.zone.id"
        @mouseleave="hoverZoneId=null"
        @focus="hoverZoneId=item.zone.id"
        @blur="hoverZoneId=null"
        @click="selectZone(item)"
      >
        <span class="hotspot-outline"></span>
      </button>
      </div>

      <div class="copper-twin-zone-labels" aria-label="厂区区域状态标签">
        <button
          v-for="item in runtimeZones"
          :key="`label-${item.zone.id}`"
          type="button"
          class="copper-zone-label"
          :class="{'is-active':item.zone.id===activeZoneId}"
          :data-status="item.status"
          :style="{left:`${hotspotFor(item.zone.id).labelX}%`,top:`${hotspotFor(item.zone.id).labelY}%`}"
          @mouseenter="hoverZoneId=item.zone.id"
          @mouseleave="hoverZoneId=null"
          @click="selectZone(item)"
        >
          <i></i><span>{{item.zone.shortName}}</span><b>{{statusText(item)}}</b>
        </button>
      </div>

      <div class="copper-twin-status-dots" aria-hidden="true">
      <i
        v-for="item in runtimeZones"
        :key="`dot-${item.zone.id}`"
        :data-status="item.status"
        :class="{ 'is-active': item.zone.id===activeZoneId }"
        :style="{left:`${hotspotFor(item.zone.id).anchorX}%`,top:`${hotspotFor(item.zone.id).anchorY}%`}"
      ></i>
      </div>
    </div>

    <aside v-if="focusZone" class="twin-focus-summary twin-focus-summary--image" :data-status="focusZone.status">
      <span>{{focusZone.zone.name}}</span>
      <strong>{{statusText(focusZone)}}</strong>
      <small>{{focusZone.zone.description}}</small>
      <b v-if="focusZone.devices.length">{{focusZone.onlineCount}} / {{focusZone.devices.length}} 在线</b>
      <b v-else>工艺区域</b>
    </aside>

    <div class="twin-legend twin-legend--image">
      <span><i data-state="normal"></i>正常</span>
      <span data-state="warning"><i></i>关注</span>
      <span data-state="alarm"><i></i>报警</span>
      <span data-state="offline"><i></i>离线</span>
    </div>
  </div>
</template>
