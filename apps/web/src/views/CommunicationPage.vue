<script setup lang="ts">
import { computed } from 'vue';
import { useSiteStore } from '../stores/site';
import { deviceInterface, deviceProtocol, qualityLabel } from '@smart-factory/domain';
import PageHeader from '../components/PageHeader.vue';
import StatusPill from '../components/StatusPill.vue';
import { formatDurationSeconds, formatInteger, formatLatency, formatRate } from '../utils/industrial-format';
const store=useSiteStore();
const avgP95=computed(()=>{const values=store.dashboard.communication.map(c=>c.latencyMsP95).filter((v):v is number=>typeof v==='number');return values.length?Math.round(values.reduce((a,b)=>a+b,0)/values.length):undefined;});
const totalErrors=computed(()=>store.dashboard.communication.reduce((s,c)=>s+(c.errorCount??0),0));
const protocols=computed(()=>[...new Set(store.dashboard.devices.map(deviceProtocol))]);
function tone(q:string):'good'|'warn'|'bad'|'muted'{return q==='GOOD'?'good':q==='BAD'?'bad':q==='STALE'?'muted':'warn';}
</script>
<template><section class="operator-page">
  <PageHeader title="工业通信" eyebrow="设备与生产" description="查看现场设备接口、通信协议、连接质量、延迟、错误和最后通信时间。"/>
  <div class="communication-status-strip"><div><span>链路</span><strong>{{store.dashboard.communication.length}}</strong></div><div><span>异常链路</span><strong :class="store.degradedLinks?'text-warning':''">{{store.degradedLinks}}</strong></div><div><span>平均 P95</span><strong>{{formatLatency(avgP95)}}</strong></div><div><span>累计错误</span><strong :class="totalErrors?'text-warning':''">{{formatInteger(totalErrors)}}</strong></div></div>
  <section class="hmi-panel communication-table-panel">
    <div class="hmi-panel-head"><div><span>通信状态</span><h2>设备通信状态</h2></div><small>实时更新</small></div>
    <div class="table-card"><table><thead><tr><th>设备</th><th>接口</th><th>协议</th><th>状态</th><th>P95</th><th>抖动</th><th>消息率</th><th>错误</th><th>最后通信</th></tr></thead><tbody>
      <tr v-for="c in store.dashboard.communication" :key="c.deviceId" :data-quality="c.quality.toLowerCase()"><td><strong class="table-primary-cell">{{store.dashboard.devices.find(d=>d.id===c.deviceId)?.name??c.deviceId}}</strong><small class="table-sub">{{c.deviceId}}</small></td><td>{{store.dashboard.devices.find(d=>d.id===c.deviceId) ? deviceInterface(store.dashboard.devices.find(d=>d.id===c.deviceId)!) : '—'}}</td><td>{{store.dashboard.devices.find(d=>d.id===c.deviceId) ? deviceProtocol(store.dashboard.devices.find(d=>d.id===c.deviceId)!) : '—'}}</td><td><StatusPill :tone="tone(c.quality)" :label="qualityLabel[c.quality]"/></td><td>{{formatLatency(c.latencyMsP95)}}</td><td>{{formatLatency(c.jitterMs)}}</td><td>{{formatRate(c.messageRatePerMin)}}</td><td>{{formatInteger(c.errorCount)}}</td><td class="table-time">{{formatDurationSeconds(c.lastSeen)}}</td></tr>
    </tbody></table></div>
  </section>
  <section class="hmi-panel protocol-register-panel"><div class="hmi-panel-head"><div><span>当前接入</span><h2>已接入协议 / 接口</h2></div><small>{{protocols.length}} 种</small></div><div class="protocol-register"><span v-for="p in protocols" :key="p">{{p}}</span></div></section>
</section></template>
