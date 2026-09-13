<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue';
import type { DistributedDevice, NativeDiscoveryStatus } from '@smart-factory/platform';
import { platform } from '../platform';

const loading = ref(true);
const error = ref<string | null>(null);
const capabilities = ref<string[]>([]);
const discoveryStatus = ref<NativeDiscoveryStatus | null>(null);
const devices = ref<DistributedDevice[]>([]);
const nativeEvents = ref<unknown[]>([]);
const watching = computed(() => discoveryStatus.value?.watching === true);
const softbusSupported = computed(() => capabilities.value.includes('softbus.discovery'));

let unsubscribe: (() => void) | null = null;

async function refresh(): Promise<void> {
  error.value = null;
  try {
    capabilities.value = await platform.getCapabilities();
    if (!softbusSupported.value) {
      discoveryStatus.value = null;
      devices.value = [];
      return;
    }
    discoveryStatus.value = await platform.invokeNativeCapability<NativeDiscoveryStatus>('softbus.status', {});
    devices.value = await platform.invokeNativeCapability<DistributedDevice[]>('softbus.discovery.snapshot', {});
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : String(cause);
  }
}

async function startWatch(): Promise<void> {
  error.value = null;
  try {
    discoveryStatus.value = await platform.invokeNativeCapability<NativeDiscoveryStatus>('softbus.discovery.watch', {});
    await refresh();
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : String(cause);
  }
}

async function stopWatch(): Promise<void> {
  error.value = null;
  try {
    discoveryStatus.value = await platform.invokeNativeCapability<NativeDiscoveryStatus>('softbus.discovery.unwatch', {});
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : String(cause);
  }
}

onMounted(async () => {
  unsubscribe = platform.onNativeEvent((event) => {
    nativeEvents.value = [event, ...nativeEvents.value].slice(0, 20);
    const e = event as { event?: string };
    if (e.event === 'softbus.deviceStateChanged') void refresh();
  });
  try {
    await refresh();
  } finally {
    loading.value = false;
  }
});

onUnmounted(() => {
  unsubscribe?.();
});
</script>

<template>
  <section>
    <p class="eyebrow">系统接口</p>
    <h1>国产系统接口</h1>
    <p class="lead">
      查看当前系统提供的原生接口和分布式设备状态。
    </p>

    <div class="two-col">
      <article class="panel">
        <div class="card-head">
          <div>
            <h2>接口状态</h2>
            <p>当前宿主可用的系统接口。</p>
          </div>
        </div>
        <p v-if="loading">正在读取...</p>
        <p v-else-if="error" class="error">{{ error }}</p>
        <template v-else>
          <div class="capability-list">
            <span v-for="item in capabilities" :key="item">{{ item }}</span>
          </div>
          <pre v-if="discoveryStatus">{{ JSON.stringify(discoveryStatus, null, 2) }}</pre>
          <p v-else class="native-note">当前系统未提供分布式设备查询。</p>
        </template>
      </article>

      <article class="panel">
        <div class="card-head">
          <div>
            <h2>分布式设备</h2>
            <p>查看系统发现的在线设备和状态变化。</p>
          </div>
          <div class="native-actions">
            <button type="button" :disabled="!softbusSupported" @click="refresh">刷新</button>
            <button v-if="!watching" type="button" :disabled="!softbusSupported" @click="startWatch">开始监听</button>
            <button v-else type="button" @click="stopWatch">停止监听</button>
          </div>
        </div>
        <div class="table-card native-table">
          <table>
            <thead><tr><th>设备</th><th>类型</th><th>状态</th><th>Network ID</th></tr></thead>
            <tbody>
              <tr v-for="device in devices" :key="device.deviceId">
                <td>{{ device.deviceName || device.deviceId }}</td>
                <td>{{ device.deviceType }}</td>
                <td>{{ device.state }}</td>
                <td><code>{{ device.networkId || '—' }}</code></td>
              </tr>
              <tr v-if="devices.length===0"><td colspan="4">暂无在线分布式设备。</td></tr>
            </tbody>
          </table>
        </div>
      </article>
    </div>

    <article class="panel native-events-panel">
      <h2>系统事件</h2>
      <p class="native-note">最近收到的系统事件。</p>
      <pre>{{ JSON.stringify(nativeEvents, null, 2) }}</pre>
    </article>
  </section>
</template>
