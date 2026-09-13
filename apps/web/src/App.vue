<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue';
import { useRoute } from 'vue-router';
import { severityLabel } from '@smart-factory/domain';
import { useSiteStore } from './stores/site';
import AppIcon from './components/AppIcon.vue';
import StatusPill from './components/StatusPill.vue';
import RuntimeDomainStrip from './components/RuntimeDomainStrip.vue';
import LoginGate from './components/LoginGate.vue';
import AccessDeniedPage from './components/AccessDeniedPage.vue';
import { roleDisplay } from './utils/operator-labels';

const site = useSiteStore();
const route = useRoute();
const now = ref(new Date());
let timer: ReturnType<typeof setInterval> | null = null;
onMounted(() => { timer = setInterval(() => { now.value = new Date(); }, 1000); });
onUnmounted(() => { if (timer) clearInterval(timer); });

const isMonitor = computed(() => route.meta.layout === 'monitor');
const currentTitle = computed(() => String(route.meta.title ?? '工业控制平台'));
const activeAlarms = computed(() => [...site.dashboard.alarms]
  .filter((a) => a.state !== 'CLEARED')
  .sort((a,b) => (a.severity === 'CRITICAL' ? -3 : a.severity === 'WARNING' ? -2 : -1) - (b.severity === 'CRITICAL' ? -3 : b.severity === 'WARNING' ? -2 : -1) || b.raisedAt-a.raisedAt));
const criticalCount = computed(() => activeAlarms.value.filter((a) => a.severity === 'CRITICAL').length);
const abnormalDevices = computed(() => site.dashboard.devices.filter((d) => d.status !== 'ONLINE').length);
const globalState = computed(() => criticalCount.value ? 'ALARM' : site.activeAlarms || abnormalDevices.value || site.degradedLinks ? 'ATTENTION' : 'NORMAL');
const globalLabel = computed(() => globalState.value === 'ALARM' ? '存在严重报警' : globalState.value === 'ATTENTION' ? '需要关注' : '运行正常');
const globalTone = computed<'good'|'warn'|'bad'>(() => globalState.value === 'ALARM' ? 'bad' : globalState.value === 'ATTENTION' ? 'warn' : 'good');
const connectionTone = computed<'info'|'live'|'warn'|'bad'|'muted'>(() => {
  if (site.dataSource === 'DEMO') return 'info';
  if (site.realtimeState === 'CONNECTED') return 'live';
  if (site.realtimeState === 'ERROR') return 'bad';
  if (site.realtimeState === 'RECONNECTING' || site.realtimeState === 'WAITING_NETWORK') return 'warn';
  return 'muted';
});
const connectionLabel = computed(() => site.dataSource === 'DEMO' ? '内置数据源' : site.realtimeState === 'CONNECTED' ? '数据在线' : site.realtimeState === 'ERROR' ? '连接异常' : site.realtimeState === 'RECONNECTING' ? '正在重连' : site.realtimeState === 'WAITING_NETWORK' ? '等待网络' : '连接未就绪');
function alarmTone(severity:string):'info'|'warn'|'bad' { return severity === 'CRITICAL' ? 'bad' : severity === 'WARNING' ? 'warn' : 'info'; }

const navGroups = computed(() => [
  { label:'业务平台', items:[
    { to:'/', label:'综合总览', icon:'overview' },
    { to:'/safety', label:'安全监测', icon:'shield' },
    { to:'/operations', label:'生产运营', icon:'history' },
    { to:'/asset-energy', label:'设备与能源', icon:'devices' },
    { to:'/intelligence', label:'智能分析', icon:'system' },
    { to:'/control', label:'可视化与控制', icon:'monitor' },
    { to:'/knowledge', label:'知识中心', icon:'count' },
    { to:'/ecosystem', label:'应用生态', icon:'scenarios' }
  ]}
]);
const showEngineering = computed(() => !site.authEnabled || ['ENGINEER','ADMINISTRATOR'].includes(site.currentUser?.role ?? ''));
const routeAccessDenied = computed(() => {
  if (route.meta.engineering && !showEngineering.value) return true;
  const permission = typeof route.meta.permission === 'string' ? route.meta.permission : '';
  return Boolean(permission && !site.can(permission as Parameters<typeof site.can>[0]));
});
const routeAccessReason = computed(() => route.meta.engineering
  ? '当前页面属于系统管理区域，仅工程师或管理员可以访问。'
  : '当前操作员没有访问此功能的权限。');
const engineeringNav = [
  { to:'/system', label:'系统管理', icon:'diagnostics', matches:['/system','/devices','/communication','/configuration','/rules','/diagnostics','/native','/compatibility','/resilience','/benchmark','/audit'] }
];
function navActive(item:{to:string;matches?:string[]}):boolean {
  if (item.matches?.some((p)=>route.path === p || route.path.startsWith(`${p}/`))) return true;
  if (item.to === '/') return route.path === '/';
  return route.path === item.to || route.path.startsWith(`${item.to}/`);
}
</script>

<template>
  <LoginGate v-if="site.authRequired" />
  <RouterView v-else-if="isMonitor" />

  <div v-else class="app-shell business-shell" :class="{ 'alarm-rail-quiet': !activeAlarms.length }">
    <aside class="sidebar">
      <div class="brand-block">
        <div class="brand-mark" aria-hidden="true">SF</div>
        <div class="brand-copy"><strong>智慧工厂数字化平台</strong><span>安全 · 生产 · 能源 · 智能</span></div>
      </div>

      <div class="sidebar-site-state">
        <span>工厂状态</span>
        <strong :data-state="globalState.toLowerCase()"><i></i>{{ globalLabel }}</strong>
      </div>

      <nav class="side-nav" aria-label="主导航">
        <template v-for="group in navGroups" :key="group.label">
          <p class="nav-caption">{{ group.label }}</p>
          <RouterLink v-for="item in group.items" :key="item.to" :to="item.to" :class="{ 'nav-active': navActive(item) }">
            <AppIcon :name="item.icon" :size="16" /><span>{{ item.label }}</span>
          </RouterLink>
        </template>
        <template v-if="showEngineering">
          <p class="nav-caption nav-caption-secondary">系统管理</p>
          <RouterLink v-for="item in engineeringNav" :key="item.to" :to="item.to" :class="{ 'nav-active': navActive(item) }">
            <AppIcon :name="item.icon" :size="16" /><span>{{ item.label }}</span>
          </RouterLink>
        </template>
      </nav>

      <div class="sidebar-foot">
        <span class="system-indicator"></span>
        <div><strong>运行系统</strong><small>{{ site.realtimeState === 'CONNECTED' ? '数据链路正常' : connectionLabel }}</small></div>
      </div>
    </aside>

    <div class="workspace">
      <header class="topbar">
        <div class="topbar-title"><span>当前页面</span><strong>{{ currentTitle }}</strong></div>
        <RuntimeDomainStrip />
        <div class="topbar-right">
          <StatusPill :tone="globalTone" :label="globalLabel" />
          <StatusPill :tone="connectionTone" :label="connectionLabel" />
          <div v-if="site.authEnabled&&site.currentUser" class="operator-identity"><strong>{{site.currentUser.displayName}}</strong><span>{{roleDisplay(site.currentUser.role)}}</span></div>
          <button v-if="site.authEnabled&&site.currentUser" type="button" class="btn-secondary compact-btn" @click="site.logout()">退出</button>
          <div class="topbar-clock"><strong>{{ now.toLocaleTimeString('zh-CN', { hour12:false }) }}</strong><span>{{ now.toLocaleDateString('zh-CN') }}</span></div>
        </div>
      </header>

      <div v-if="['RECONNECTING','WAITING_NETWORK','ERROR'].includes(site.realtimeState)" class="connection-banner" :data-state="site.realtimeState.toLowerCase()">
        <div class="connection-copy"><span class="connection-light"></span><strong>{{ site.connectionLabel }}</strong><span>{{ site.fallbackReason || `实时连接 ${connectionLabel} · 重连 ${site.realtimeDiagnostics.reconnectCount} 次` }}</span></div>
        <button type="button" class="btn-secondary" :disabled="site.loading" @click="site.retryBackend()">{{ site.loading?'连接中':'重新连接' }}</button>
      </div>

      <main class="page"><AccessDeniedPage v-if="routeAccessDenied" :title="currentTitle" :reason="routeAccessReason"/><RouterView v-else /></main>

      <footer class="operator-statusbar">
        <span><i :data-state="site.realtimeState==='CONNECTED'?'normal':'warning'"></i>数据 {{ site.dataOriginLabel }}</span>
        <span>实时连接 {{ connectionLabel }}</span>
        <span>通信异常 {{ site.degradedLinks }}</span>
        <span>活动报警 {{ site.activeAlarms }}</span><span v-if="site.currentUser">操作员 {{site.currentUser.displayName}}</span>
        <span>数据时间 {{ new Date(site.dashboard.generatedAt).toLocaleTimeString('zh-CN',{hour12:false}) }}</span>
      </footer>
    </div>

    <aside class="alarm-rail" :data-quiet="activeAlarms.length ? 'false' : 'true'">
      <template v-if="activeAlarms.length">
        <div class="alarm-rail-head">
          <div><span>安全事件</span><strong>活动报警</strong></div>
          <span class="alarm-rail-count" :data-alarm="criticalCount>0">{{ activeAlarms.length }}</span>
        </div>
        <div class="alarm-rail-body">
          <article v-for="alarm in activeAlarms.slice(0,8)" :key="alarm.id" class="rail-alarm" :data-severity="alarm.severity.toLowerCase()">
            <div class="rail-alarm-meta"><StatusPill :tone="alarmTone(alarm.severity)" :label="severityLabel[alarm.severity]"/><time>{{new Date(alarm.raisedAt).toLocaleTimeString('zh-CN',{hour12:false})}}</time></div>
            <strong>{{ alarm.title }}</strong>
            <span>{{ site.dashboard.devices.find((d)=>d.id===alarm.deviceId)?.name ?? alarm.deviceId }}</span>
            <p>{{ alarm.message }}</p>
          </article>
        </div>
        <RouterLink class="alarm-rail-link" to="/alarms">查看全部报警</RouterLink>
      </template>
      <RouterLink v-else class="alarm-rail-quiet-link" to="/alarms" title="当前无活动报警，点击查看报警记录">
        <span class="alarm-quiet-symbol">✓</span>
        <strong>正常</strong>
        <small>报警 0</small>
      </RouterLink>
    </aside>
  </div>
</template>
