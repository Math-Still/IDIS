<script setup lang="ts">
import { computed } from 'vue';
import {
  deviceStatusLabel,
  qualityLabel,
  type Alarm,
  type CommunicationHealth,
  type Device,
  type PointDefinition,
  type PointRef,
  type ScenarioType,
  type TelemetryPoint
} from '@smart-factory/domain';
import StatusPill from './StatusPill.vue';
import { formatDurationSeconds, formatLatency, formatTelemetryText } from '../utils/industrial-format';

const props = defineProps<{
  type: ScenarioType;
  devices: Device[];
  telemetry: TelemetryPoint[];
  communication: CommunicationHealth[];
  alarms: Alarm[];
  bindings?: Record<string,PointRef>;
  strictBindings?: boolean;
}>();

const activeAlarms = computed(() => props.alarms.filter((alarm) => alarm.state !== 'CLEARED'));
const onlineCount = computed(() => props.devices.filter((device) => device.status !== 'OFFLINE').length);
const latestTs = computed(() => Math.max(0, ...props.telemetry.map((point) => point.receiveTs)));
const worstCommunication = computed(() => {
  const rank = (quality: CommunicationHealth['quality']) => quality === 'BAD' ? 4 : quality === 'STALE' ? 3 : quality === 'UNCERTAIN' ? 2 : 1;
  return [...props.communication].sort((a,b) => rank(b.quality) - rank(a.quality) || (b.latencyMsP95??-1) - (a.latencyMsP95??-1))[0] ?? null;
});

function point(...keys: string[]): TelemetryPoint | undefined {
  const normalized = keys.map((key) => key.toLowerCase());
  return props.telemetry.find((item) => {
    const id = item.pointId.toLowerCase();
    const label = item.label.toLowerCase();
    return normalized.some((key) => id === key || id.includes(key) || label.includes(key));
  });
}
function boundPoint(role:string, ...legacyKeys:string[]): TelemetryPoint | undefined {
  if (props.bindings) {
    const ref = props.bindings[role];
    if (!ref) return undefined;
    return props.telemetry.find((item) => item.deviceId === ref.deviceId && item.pointId === ref.pointId);
  }
  if (props.strictBindings) return undefined;
  return point(...legacyKeys);
}
function pointsBy(...keys: string[]): TelemetryPoint[] {
  const normalized = keys.map((key) => key.toLowerCase());
  return props.telemetry.filter((item) => {
    const id = item.pointId.toLowerCase();
    const label = item.label.toLowerCase();
    return normalized.some((key) => id === key || id.includes(key) || label.includes(key));
  });
}
function pointDefinition(item?:TelemetryPoint):PointDefinition|undefined {
  if(!item)return undefined;
  return props.devices.find((device)=>device.id===item.deviceId)?.pointDefinitions?.find((definition)=>definition.pointId===item.pointId);
}
function limit(item:TelemetryPoint|undefined,key:'warningLow'|'warningHigh'|'alarmLow'|'alarmHigh'|'setpoint'|'deadband'):number|undefined {
  return pointDefinition(item)?.limits?.[key];
}
function limitText(item:TelemetryPoint|undefined,key:'warningLow'|'warningHigh'|'alarmLow'|'alarmHigh'|'setpoint'):string {
  const configured=limit(item,key);if(configured===undefined)return '未配置';
  const unit=item?.unit?` ${item.unit}`:'';return `${configured}${unit}`;
}
function rangeText(item:TelemetryPoint|undefined,kind:'warning'|'alarm'):string {
  const low=limit(item,kind==='warning'?'warningLow':'alarmLow');const high=limit(item,kind==='warning'?'warningHigh':'alarmHigh');
  const unit=item?.unit?` ${item.unit}`:'';
  if(low!==undefined&&high!==undefined)return `${low}–${high}${unit}`;
  if(low!==undefined)return `≥ ${low}${unit}`;
  if(high!==undefined)return `≤ ${high}${unit}`;
  return '未配置';
}
function remoteControlText():string {
  const count=props.devices.flatMap((device)=>Object.values(device.commandCapabilities??{})).filter((cap)=>cap.executionClass!=='EMERGENCY'&&cap.actionId!=='device.selfTest').length;
  return count>0?`已登记 ${count} 项`:'当前设备未登记';
}
function value(item?: TelemetryPoint): string { return formatTelemetryText(item); }
function quality(item?: TelemetryPoint): string { return item ? qualityLabel[item.quality] : '无数据'; }
function dataTone(item?: TelemetryPoint): 'good'|'warn'|'bad'|'muted' {
  if (!item) return 'muted';
  if (item.quality === 'BAD') return 'bad';
  if (item.quality === 'UNCERTAIN') return 'warn';
  if (item.quality === 'STALE') return 'muted';
  return 'good';
}
function deviceTone(status: Device['status']): 'good'|'warn'|'info'|'muted' {
  return status === 'ONLINE' ? 'good' : status === 'DEGRADED' ? 'warn' : status === 'MAINTENANCE' ? 'info' : 'muted';
}
function communicationQuality(deviceId: string): string {
  const item = props.communication.find((health) => health.deviceId === deviceId);
  return item ? qualityLabel[item.quality] : '无数据';
}

const temperature = computed(() => boundPoint('ambientTemperature','temperature','温度'));
const humidity = computed(() => boundPoint('ambientHumidity','humidity','湿度'));
const occupied = computed(() => boundPoint('occupied','occupied','presence','pir','人员','感应'));
const lightState = computed(() => boundPoint('lightState','lightstate','light_state','lamp','照明状态','灯状态'));
const lightMode = computed(() => boundPoint('lightMode','lightmode','mode','模式'));
const gasPoints = computed(() => {
  if (props.bindings || props.strictBindings) { const item=boundPoint('gasConcentration'); return item?[item]:[]; }
  const selected = pointsBy('co','so2','h2s','nh3','ch4','gas','level','浓度');
  return selected.length ? selected : props.telemetry.filter((item) => typeof item.value === 'number');
});
const agvDistance = computed(() => boundPoint('obstacleDistance','frontdistance','distance','障碍距离','前向距离'));
const agvMotion = computed(() => boundPoint('motionState','motionstate','motion','runstate','运动状态','运行状态'));
const agvSpeed = computed(() => boundPoint('speed','speed','速度'));
const agvPosition = computed(() => boundPoint('position','position','位置'));
const agvTask = computed(() => boundPoint('task','task','任务'));
const totalCount = computed(() => boundPoint('totalCount','totalcount','count','累计计数','当前计数'));
const countRate = computed(() => boundPoint('countRate','rate','计数速率','当前速率'));
const countTarget = computed(() => boundPoint('target','target','目标'));

const commonStatusText = computed(() => {
  if (activeAlarms.value.some((alarm) => alarm.severity === 'CRITICAL')) return '存在严重报警';
  if (activeAlarms.value.length) return '需要关注';
  if (onlineCount.value < props.devices.length) return '部分设备离线';
  if (worstCommunication.value && worstCommunication.value.quality !== 'GOOD') return '通信质量下降';
  return '运行正常';
});
const commonStatusTone = computed<'good'|'warn'|'bad'>(() => commonStatusText.value === '存在严重报警' ? 'bad' : commonStatusText.value === '运行正常' ? 'good' : 'warn');
</script>

<template>
  <section class="scenario-process-panel" :data-scenario="type">
    <div class="scenario-operation-strip">
      <div class="scenario-operation-state">
        <span>当前状态</span>
        <StatusPill :tone="commonStatusTone" :label="commonStatusText" />
      </div>
      <div><span>设备在线</span><strong>{{ onlineCount }} / {{ devices.length }}</strong></div>
      <div><span>活动报警</span><strong :class="activeAlarms.length ? 'text-warning' : ''">{{ activeAlarms.length }}</strong></div>
      <div><span>通信</span><strong>{{ worstCommunication ? qualityLabel[worstCommunication.quality] : '无数据' }}</strong></div>
      <div><span>最近数据</span><strong>{{ latestTs ? formatDurationSeconds(latestTs) : '---' }}</strong></div>
    </div>

    <div v-if="type === 'temperature_humidity'" class="scenario-business-grid scenario-business-grid-environment">
      <section class="scenario-primary-instruments">
        <div class="scenario-instrument" :data-quality="temperature?.quality.toLowerCase() ?? 'missing'">
          <span>当前温度</span><strong>{{ value(temperature) }}</strong><small>{{ quality(temperature) }}</small>
        </div>
        <div class="scenario-instrument" :data-quality="humidity?.quality.toLowerCase() ?? 'missing'">
          <span>当前湿度</span><strong>{{ value(humidity) }}</strong><small>{{ quality(humidity) }}</small>
        </div>
      </section>
      <dl class="scenario-condition-list">
        <div><dt>温度预警范围</dt><dd>{{ rangeText(temperature,'warning') }}</dd></div>
        <div><dt>湿度预警范围</dt><dd>{{ rangeText(humidity,'warning') }}</dd></div>
        <div><dt>传感器状态</dt><dd>{{ devices[0] ? deviceStatusLabel[devices[0].status] : '无设备' }}</dd></div>
        <div><dt>通信质量</dt><dd>{{ worstCommunication ? qualityLabel[worstCommunication.quality] : '无数据' }}</dd></div>
      </dl>
    </div>

    <div v-else-if="type === 'pir_lighting'" class="scenario-business-grid">
      <section class="scenario-primary-instruments">
        <div class="scenario-instrument" :data-quality="occupied?.quality.toLowerCase() ?? 'missing'">
          <span>目标检测</span><strong>{{ value(occupied) }}</strong><small>{{ quality(occupied) }}</small>
        </div>
        <div class="scenario-instrument" :data-quality="lightState?.quality.toLowerCase() ?? 'missing'">
          <span>照明反馈</span><strong>{{ value(lightState) }}</strong><small>{{ lightState ? quality(lightState) : '当前设备未提供反馈点' }}</small>
        </div>
      </section>
      <dl class="scenario-condition-list">
        <div><dt>运行模式</dt><dd>{{ lightMode ? value(lightMode) : '未提供' }}</dd></div>
        <div><dt>远程操作</dt><dd>{{ remoteControlText() }}</dd></div>
        <div><dt>设备状态</dt><dd>{{ devices[0] ? deviceStatusLabel[devices[0].status] : '无设备' }}</dd></div>
        <div><dt>通信质量</dt><dd>{{ worstCommunication ? qualityLabel[worstCommunication.quality] : '无数据' }}</dd></div>
      </dl>
    </div>

    <div v-else-if="type === 'hazardous_gas'" class="scenario-business-grid scenario-business-grid-gas">
      <section class="scenario-gas-readings">
        <div v-for="item in gasPoints" :key="`${item.deviceId}:${item.pointId}`" class="scenario-gas-reading" :data-quality="item.quality.toLowerCase()">
          <span>{{ item.label }}</span><strong>{{ value(item) }}</strong><StatusPill :tone="dataTone(item)" :label="quality(item)" />
        </div>
        <div v-if="!gasPoints.length" class="scenario-unavailable">暂无气体测量值</div>
      </section>
      <dl class="scenario-condition-list">
        <div><dt>预警阈值</dt><dd>{{ limitText(gasPoints[0],'warningHigh') }}</dd></div>
        <div><dt>危险阈值</dt><dd>{{ limitText(gasPoints[0],'alarmHigh') }}</dd></div>
        <div><dt>探头状态</dt><dd>{{ devices[0] ? deviceStatusLabel[devices[0].status] : '无设备' }}</dd></div>
        <div><dt>活动报警</dt><dd :class="activeAlarms.length ? 'text-warning' : ''">{{ activeAlarms.length }} 条</dd></div>
      </dl>
    </div>

    <div v-else-if="type === 'agv_obstacle'" class="scenario-business-grid">
      <section class="scenario-primary-instruments">
        <div class="scenario-instrument scenario-instrument-prominent" :data-quality="agvDistance?.quality.toLowerCase() ?? 'missing'">
          <span>前方障碍距离</span><strong>{{ value(agvDistance) }}</strong><small>{{ quality(agvDistance) }}</small>
        </div>
        <div class="scenario-instrument" :data-quality="agvMotion?.quality.toLowerCase() ?? 'missing'">
          <span>运行状态</span><strong>{{ value(agvMotion) }}</strong><small>{{ quality(agvMotion) }}</small>
        </div>
      </section>
      <dl class="scenario-condition-list">
        <div><dt>减速距离</dt><dd>{{ limitText(agvDistance,'warningLow') }}</dd></div>
        <div><dt>停车距离</dt><dd>{{ limitText(agvDistance,'alarmLow') }}</dd></div>
        <div><dt>速度</dt><dd>{{ agvSpeed ? value(agvSpeed) : '---' }}</dd></div>
        <div><dt>位置</dt><dd>{{ agvPosition ? value(agvPosition) : '未接入' }}</dd></div>
        <div><dt>任务状态</dt><dd>{{ agvTask ? value(agvTask) : '未接入' }}</dd></div>
        <div><dt>通信质量</dt><dd>{{ worstCommunication ? qualityLabel[worstCommunication.quality] : '无数据' }}</dd></div>
      </dl>
    </div>

    <div v-else-if="type === 'goods_counting'" class="scenario-business-grid">
      <section class="scenario-primary-instruments">
        <div class="scenario-instrument scenario-instrument-prominent" :data-quality="totalCount?.quality.toLowerCase() ?? 'missing'">
          <span>当前累计</span><strong>{{ value(totalCount) }}</strong><small>{{ quality(totalCount) }}</small>
        </div>
        <div class="scenario-instrument" :data-quality="countRate?.quality.toLowerCase() ?? 'missing'">
          <span>计数速率</span><strong>{{ value(countRate) }}</strong><small>{{ quality(countRate) }}</small>
        </div>
      </section>
      <dl class="scenario-condition-list">
        <div><dt>目标数量</dt><dd>{{ countTarget ? value(countTarget) : limitText(totalCount,'setpoint') }}</dd></div>
        <div><dt>计数传感器</dt><dd>{{ devices[0] ? deviceStatusLabel[devices[0].status] : '无设备' }}</dd></div>
        <div><dt>通信质量</dt><dd>{{ worstCommunication ? qualityLabel[worstCommunication.quality] : '无数据' }}</dd></div>
        <div><dt>活动报警</dt><dd :class="activeAlarms.length ? 'text-warning' : ''">{{ activeAlarms.length }} 条</dd></div>
      </dl>
    </div>

    <div class="scenario-device-status-table">
      <div class="scenario-device-status-head"><span>设备</span><span>状态</span><span>位置</span><span>通信</span><span>最后数据</span></div>
      <RouterLink v-for="device in devices" :key="device.id" :to="`/devices/${device.id}`" class="scenario-device-status-row">
        <strong>{{ device.name }}</strong>
        <StatusPill :tone="deviceTone(device.status)" :label="deviceStatusLabel[device.status]" />
        <span>{{ device.location }}</span>
        <span>{{ communicationQuality(device.id) }}</span>
        <span>{{ formatDurationSeconds(device.lastSeen) }}</span>
      </RouterLink>
      <div v-if="!devices.length" class="scenario-unavailable">当前场景尚未配置设备</div>
    </div>
  </section>
</template>
