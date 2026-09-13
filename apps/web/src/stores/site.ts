import { computed, ref } from 'vue';
import { defineStore } from 'pinia';
import {
  applyCommandEvent,
  applyFreshness,
  classifyTelemetryOrder,
  expireCommand,
  observeCommand,
  timeoutCommand,
  validateAlarm,
  validateCommand,
  validateCommunicationHealth,
  validateDashboardSnapshot,
  validateDevice,
  validateTelemetryPoint,
  type Alarm,
  type AlarmActionRequest,
  type AlarmHistoryResponse,
  type AuditHistoryResponse,
  type AuthUser,
  type Permission,
  type CommandObservation,
  type Command,
  type CommandEvent,
  type CommandRequest,
  type CommunicationHealth,
  type DashboardSnapshot,
  type Device,
  type TelemetryHistoryResponse,
  type TelemetryPoint,
  type ApplicationInstance,
  type PlatformAsset,
  type PlatformConfigSnapshot,
  type PlatformConfigStatus,
  type ConfigValidationResponse,
  type ConfigPublishResponse,
  type PlatformRule,
  type AlarmOccurrenceV2,
  type IncidentV2,
  type TimelineResponseV2,
  type CommandPreviewV2,
  type CommandReconciliationInput,
  type ReplaySessionV2,
  type ReportJobV2,
  type PlatformTelemetryHistoryResponse,
  type AgentStatusV2,
  type AgentReplyV2
} from '@smart-factory/domain';
import {
  ApiError,
  BrowserWebSocketClient,
  RestApiClient,
  PlatformApiClient,
  type RealtimeClient,
  type RealtimeDiagnostics,
  type RealtimeMessage,
  type RealtimeState
} from '@smart-factory/api-client';
import type { RuntimeConfig } from '@smart-factory/contracts';
import { demoDashboard } from '../mock/dashboard';
import { loadRuntimeConfig } from '../runtime-config';

type DataSource = 'DEMO' | 'BACKEND';
type SourceMode = 'AUTO' | 'DEMO' | 'BACKEND';

const cloneDemo = (): DashboardSnapshot => {
  const copy: DashboardSnapshot = typeof structuredClone === 'function'
    ? structuredClone(demoDashboard)
    : JSON.parse(JSON.stringify(demoDashboard)) as DashboardSnapshot;
  const delta = Date.now() - copy.generatedAt;
  copy.generatedAt += delta;
  copy.devices = copy.devices.map((d) => ({ ...d, lastSeen: d.lastSeen + delta }));
  copy.telemetry = copy.telemetry.map((t) => ({ ...t, sampleTs: t.sampleTs + delta, receiveTs: t.receiveTs + delta }));
  copy.alarms = copy.alarms.map((a) => ({
    ...a, raisedAt: a.raisedAt + delta,
    acknowledgedAt: a.acknowledgedAt === undefined ? undefined : a.acknowledgedAt + delta,
    clearedAt: a.clearedAt === undefined ? undefined : a.clearedAt + delta
  }));
  copy.commands = copy.commands.map((c) => ({
    ...c, issuedAt: c.issuedAt + delta, updatedAt: c.updatedAt + delta,
    expiresAt: c.expiresAt === undefined ? undefined : c.expiresAt + delta
  }));
  copy.communication = copy.communication.map((c) => ({ ...c, lastSeen: c.lastSeen===undefined?undefined:c.lastSeen + delta }));
  return copy;
};

const emptyDashboard = (): DashboardSnapshot => ({
  siteName: '等待后端数据',
  generatedAt: Date.now(),
  provenance: { originKind:'UNKNOWN', sourceId:'unverified', deliveryMode:'LIVE' },
  devices: [],
  telemetry: [],
  alarms: [],
  commands: [],
  communication: [],
  scenarios: []
});

function upsertByKey<T>(items: T[], incoming: T, key: (item: T) => string): T[] {
  const incomingKey = key(incoming);
  const index = items.findIndex((item) => key(item) === incomingKey);
  if (index < 0) return [...items, incoming];
  const copy = [...items];
  copy[index] = incoming;
  return copy;
}

function createCommandId(): string {
  if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') return crypto.randomUUID();
  return `cmd-${Date.now()}-${Math.random().toString(36).slice(2, 12)}`;
}

function createIdempotencyKey(): string {
  if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') return `idem-${crypto.randomUUID()}`;
  return `idem-${Date.now()}-${Math.random().toString(36).slice(2, 14)}`;
}

function initialRealtimeDiagnostics(): RealtimeDiagnostics {
  return {
    state: 'IDLE', reconnectCount: 0, parseErrorCount: 0, socketErrorCount: 0,
    silenceTimeoutCount: 0, rejectedFrameCount: 0, subscriberErrorCount: 0,
    lastConnectedAt: null, lastDisconnectedAt: null, lastMessageAt: null,
    nextReconnectDelayMs: null, lastCloseCode: null, lastCloseReason: null,
    networkOnline: true, backoffAttempt: 0, stableSinceAt: null,
    lastStateChangeAt: Date.now()
  };
}

export const useSiteStore = defineStore('site', () => {
  // Never render fabricated process values before runtime-config is resolved.
  // Demo data is entered only after an explicit demo/auto fallback decision.
  const dashboard = ref<DashboardSnapshot>(emptyDashboard());
  const dataSource = ref<DataSource>('BACKEND');
  const sourceMode = ref<SourceMode>('AUTO');
  const loading = ref(false);
  const error = ref<string | null>(null);
  const fallbackReason = ref<string | null>(null);
  const realtimeState = ref<RealtimeState>('IDLE');
  const realtimeDiagnostics = ref<RealtimeDiagnostics>(initialRealtimeDiagnostics());
  const lastRealtimeError = ref<string | null>(null);
  const ignoredCommandEvents = ref(0);
  const duplicateCommandEvents = ref(0);
  const rejectedRealtimeMessages = ref(0);
  const ignoredTelemetryEvents = ref(0);
  const duplicateTelemetryEvents = ref(0);
  const commandObservations = ref<Record<string,CommandObservation>>({});
  const authEnabled = ref(false);
  const authRequired = ref(false);
  const authError = ref<string | null>(null);
  const currentUser = ref<AuthUser | null>(null);
  const accessToken = ref<string | null>(null);
  const platformConfigStatus = ref<PlatformConfigStatus | null>(null);
  const platformAssets = ref<PlatformAsset[]>([]);
  const applicationInstances = ref<ApplicationInstance[]>([]);
  const platformRules = ref<PlatformRule[]>([]);
  const alarmOccurrencesV2 = ref<AlarmOccurrenceV2[]>([]);
  const incidentsV2 = ref<IncidentV2[]>([]);
  const agentStatus = ref<AgentStatusV2 | null>(null);

  let config: RuntimeConfig | null = null;
  let api: RestApiClient | null = null;
  let platformApi: PlatformApiClient | null = null;
  let realtime: RealtimeClient | null = null;
  let freshnessTimer: ReturnType<typeof setInterval> | null = null;
  let commandTimer: ReturnType<typeof setInterval> | null = null;
  let unsubscribeMessage: (() => void) | null = null;
  let unsubscribeState: (() => void) | null = null;

  const onlineDevices = computed(() => dashboard.value.devices.filter((d) => d.status !== 'OFFLINE').length);
  const activeAlarms = computed(() => dashboard.value.alarms.filter((a) => a.state !== 'CLEARED').length);
  const degradedLinks = computed(() => dashboard.value.communication.filter((c) => c.quality !== 'GOOD').length);
  const dataOrigin = computed(() => dashboard.value.provenance?.originKind ?? 'UNKNOWN');
  const dataOriginLabel = computed(() => {
    if (dataSource.value === 'DEMO') return '内置数据源';
    if (dataOrigin.value === 'SIMULATION') return '内置数据源';
    if (dataOrigin.value === 'REAL_DEVICE') return '真实采集';
    return '来源未知';
  });
  const connectionLabel = computed(() => {
    if (dataSource.value === 'DEMO') return '内置数据源';
    if (realtimeState.value === 'CONNECTED') return '实时连接';
    if (realtimeState.value === 'RECONNECTING') return '正在重连';
    if (realtimeState.value === 'WAITING_NETWORK') return '等待网络恢复';
    if (realtimeState.value === 'ERROR') return '实时链路异常';
    return '后端快照';
  });
  const currentRole = computed(() => currentUser.value?.role ?? (authEnabled.value ? 'OBSERVER' : 'ADMINISTRATOR'));
  function can(permission: Permission): boolean {
    return !authEnabled.value || Boolean(currentUser.value?.permissions.includes(permission));
  }

  function useDemo(reason?: string): void {
    dashboard.value = validateDashboardSnapshot({ ...cloneDemo(), generatedAt: Date.now() });
    dataSource.value = 'DEMO';
    fallbackReason.value = reason ?? null;
    error.value = reason ?? null;
  }

  function stopRealtime(): void {
    unsubscribeMessage?.();
    unsubscribeState?.();
    unsubscribeMessage = null;
    unsubscribeState = null;
    realtime?.disconnect();
    realtime = null;
    realtimeState.value = 'DISCONNECTED';
  }

  function refreshDiagnostics(): void {
    if (realtime) realtimeDiagnostics.value = realtime.getDiagnostics();
  }


  function refreshScenarioCounters(): void {
    dashboard.value.scenarios = dashboard.value.scenarios.map((scenario) => {
      const deviceIds = new Set(dashboard.value.devices.filter((d) => d.scenario === scenario.type).map((d) => d.id));
      return {
        ...scenario,
        deviceCount: deviceIds.size,
        onlineCount: dashboard.value.devices.filter((d) => deviceIds.has(d.id) && d.status !== 'OFFLINE').length,
        activeAlarmCount: dashboard.value.alarms.filter((a) => deviceIds.has(a.deviceId) && a.state !== 'CLEARED').length
      };
    });
  }

  function upsertTelemetry(point: TelemetryPoint): boolean {
    validateTelemetryPoint(point);
    if (!dashboard.value.devices.some((d) => d.id === point.deviceId)) throw new Error(`Telemetry references unknown device: ${point.deviceId}`);
    const current = dashboard.value.telemetry.find((i) => i.deviceId === point.deviceId && i.pointId === point.pointId);
    if (current) {
      const order = classifyTelemetryOrder(current, point);
      if (order === 'OUT_OF_ORDER') { ignoredTelemetryEvents.value += 1; return false; }
      if (order === 'DUPLICATE') { duplicateTelemetryEvents.value += 1; return false; }
    }
    dashboard.value.telemetry = upsertByKey(dashboard.value.telemetry, point, (i) => `${i.deviceId}:${i.pointId}`);
    return true;
  }

  function upsertAlarm(alarm: Alarm): void {
    validateAlarm(alarm);
    const platformAlarm = alarm.deviceId === 'platform' && alarm.id.startsWith('system-');
    if (!platformAlarm && !dashboard.value.devices.some((d) => d.id === alarm.deviceId)) throw new Error(`Alarm references unknown device: ${alarm.deviceId}`);
    dashboard.value.alarms = upsertByKey(dashboard.value.alarms, alarm, (i) => i.id);
    refreshScenarioCounters();
  }

  function upsertDevice(device: Device): void {
    validateDevice(device);
    dashboard.value.devices = upsertByKey(dashboard.value.devices, device, (i) => i.id);
    refreshScenarioCounters();
  }

  function upsertCommunication(health: CommunicationHealth): void {
    validateCommunicationHealth(health);
    if (!dashboard.value.devices.some((d) => d.id === health.deviceId)) throw new Error(`Communication references unknown device: ${health.deviceId}`);
    dashboard.value.communication = upsertByKey(dashboard.value.communication, health, (i) => i.deviceId);
  }

  function upsertCommand(command: Command): void {
    validateCommand(command);
    if (!dashboard.value.devices.some((d) => d.id === command.deviceId)) throw new Error(`Command references unknown device: ${command.deviceId}`);
    dashboard.value.commands = upsertByKey(dashboard.value.commands, command, (i) => i.id);
  }

  function applyCommandStatus(event: CommandEvent): void {
    const current = dashboard.value.commands.find((c) => c.id === event.commandId);
    if (!current) {
      ignoredCommandEvents.value += 1;
      return;
    }
    const decision = applyCommandEvent(current, event);
    if (!decision.accepted) {
      ignoredCommandEvents.value += 1;
      return;
    }
    if (decision.duplicate) duplicateCommandEvents.value += 1;
    upsertCommand(decision.command);
  }

  function applyRealtimeMessage(message: RealtimeMessage): void {
    try {
      let mutated = false;
      switch (message.type) {
        case 'telemetry.updated': mutated = upsertTelemetry(message.data as TelemetryPoint); break;
        case 'alarm.updated': upsertAlarm(message.data as Alarm); mutated = true; break;
        case 'device.updated': upsertDevice(message.data as Device); mutated = true; break;
        case 'communication.updated': upsertCommunication(message.data as CommunicationHealth); mutated = true; break;
        case 'command.status': applyCommandStatus(message.data as CommandEvent); mutated = true; break;
        case 'dashboard.snapshot': dashboard.value = validateDashboardSnapshot(message.data as DashboardSnapshot); mutated = true; break;
        case 'heartbeat': break;
        default: break;
      }
      if (mutated && message.type !== 'dashboard.snapshot') dashboard.value.generatedAt = message.timestamp || Date.now();
    } catch (cause) {
      rejectedRealtimeMessages.value += 1;
      lastRealtimeError.value = cause instanceof Error ? cause.message : String(cause);
    }
    refreshDiagnostics();
  }

  function startRealtime(runtime: RuntimeConfig): void {
    stopRealtime();
    const policy = {
      baseDelayMs: runtime.realtime?.reconnectBaseMs ?? 1_000,
      maxDelayMs: runtime.realtime?.reconnectMaxMs ?? 30_000,
      jitterRatio: runtime.realtime?.reconnectJitterRatio ?? 0.2,
      stableConnectionMs: runtime.realtime?.stableConnectionMs ?? 10_000,
      messageSilenceReconnectMs: runtime.realtime?.messageSilenceReconnectMs ?? 45_000,
      maxMessageBytes: runtime.transport?.maxRealtimeMessageBytes ?? 512 * 1024
    };
    realtime = new BrowserWebSocketClient(runtime.wsBaseUrl, policy, undefined, accessToken.value);
    unsubscribeState = realtime.onState((state) => {
      realtimeState.value = state;
      refreshDiagnostics();
      if (state === 'CONNECTED') lastRealtimeError.value = null;
      if (state === 'ERROR') lastRealtimeError.value = 'WebSocket transport error';
    });
    unsubscribeMessage = realtime.onMessage(applyRealtimeMessage);
    realtime.connect();
  }

  async function loadBackend(runtime: RuntimeConfig): Promise<boolean> {
    if (!api) api = new RestApiClient(runtime);
    if (!platformApi) platformApi = new PlatformApiClient(runtime);
    api.setAccessToken(accessToken.value);
    platformApi.setAccessToken(accessToken.value);
    try {
      const snapshot = validateDashboardSnapshot(await api.getDashboard());
      dashboard.value = snapshot;
      dataSource.value = 'BACKEND';
      fallbackReason.value = null;
      error.value = null;
      try {
        const [status, assets, instances] = await Promise.all([platformApi.getConfigStatus(), platformApi.getAssets(), platformApi.getApplicationInstances()]);
        platformConfigStatus.value = status; platformAssets.value = assets; applicationInstances.value = instances;
      } catch {
        platformConfigStatus.value = null; platformAssets.value = []; applicationInstances.value = [];
      }
      startRealtime(runtime);
      return true;
    } catch (cause) {
      const message = cause instanceof Error ? cause.message : String(cause);
      if ((runtime.dataMode ?? 'auto') === 'backend') {
        stopRealtime();
        dashboard.value = emptyDashboard();
        dataSource.value = 'BACKEND';
        error.value = message;
        fallbackReason.value = null;
      } else {
        useDemo(`实时数据接口不可用，当前使用内置数据源：${message}`);
      }
      return false;
    }
  }

  async function initialize(): Promise<void> {
    loading.value = true;
    try {
      config = await loadRuntimeConfig();
      const requestedMode = config.dataMode ?? 'auto';
      sourceMode.value = requestedMode.toUpperCase() as SourceMode;
      if (requestedMode === 'demo') {
        authEnabled.value = false;
        authRequired.value = false;
        useDemo('运行配置已启用内置数据源。');
        return;
      }

      api = new RestApiClient(config);
      platformApi = new PlatformApiClient(config);
      const authStatus = await api.getAuthStatus();
      authEnabled.value = authStatus.authEnabled ?? false;
      if (authEnabled.value) {
        const savedToken = typeof sessionStorage !== 'undefined' ? sessionStorage.getItem('smart-factory.access-token') : null;
        if (savedToken) {
          accessToken.value = savedToken;
          api.setAccessToken(savedToken);
          try {
            const session = await api.getMe();
            currentUser.value = session.user;
            authRequired.value = false;
            authError.value = null;
          } catch {
            accessToken.value = null;
            currentUser.value = null;
            if (typeof sessionStorage !== 'undefined') sessionStorage.removeItem('smart-factory.access-token');
            authRequired.value = true;
          }
        } else {
          authRequired.value = true;
        }
        if (authRequired.value) {
          stopRealtime();
          dashboard.value = emptyDashboard();
          dataSource.value = 'BACKEND';
          fallbackReason.value = null;
          error.value = null;
          return;
        }
      } else {
        authRequired.value = false;
        currentUser.value = null;
      }
      await loadBackend(config);
    } catch (cause) {
      const message = cause instanceof Error ? cause.message : String(cause);
      stopRealtime();
      dashboard.value = emptyDashboard();
      dataSource.value = 'BACKEND';
      sourceMode.value = 'BACKEND';
      error.value = `运行配置或后端初始化失败，系统未启用内置数据源：${message}`;
      fallbackReason.value = null;
    } finally {
      loading.value = false;
      startTimers();
    }
  }

  async function login(userId: string, token: string): Promise<boolean> {
    if (!config) config = await loadRuntimeConfig();
    if (!api) api = new RestApiClient(config);
    authError.value = null;
    loading.value = true;
    try {
      api.setAccessToken(null);
      const session = await api.login(userId.trim(), token);
      if (!session.authEnabled) {
        authEnabled.value = false;
        authRequired.value = false;
        currentUser.value = session.user;
        return await loadBackend(config);
      }
      accessToken.value = token;
      api.setAccessToken(token);
      platformApi?.setAccessToken(token);
      currentUser.value = session.user;
      authEnabled.value = true;
      authRequired.value = false;
      if (typeof sessionStorage !== 'undefined') sessionStorage.setItem('smart-factory.access-token', token);
      return await loadBackend(config);
    } catch (cause) {
      authRequired.value = true;
      currentUser.value = null;
      accessToken.value = null;
      if (typeof sessionStorage !== 'undefined') sessionStorage.removeItem('smart-factory.access-token');
      authError.value = cause instanceof Error ? cause.message : String(cause);
      return false;
    } finally { loading.value = false; }
  }

  function logout(): void {
    stopRealtime();
    accessToken.value = null;
    currentUser.value = null;
    authError.value = null;
    if (typeof sessionStorage !== 'undefined') sessionStorage.removeItem('smart-factory.access-token');
    if (authEnabled.value) {
      authRequired.value = true;
      dashboard.value = emptyDashboard();
      dataSource.value = 'BACKEND';
    }
    api?.setAccessToken(null);
    platformApi?.setAccessToken(null);
    platformConfigStatus.value = null; platformAssets.value = []; applicationInstances.value = [];
  }

  async function retryBackend(): Promise<boolean> {
    if (authEnabled.value && authRequired.value) return false;
    if (!config) config = await loadRuntimeConfig();
    loading.value = true;
    try { return await loadBackend(config); }
    finally { loading.value = false; }
  }

  async function issueCommand(request: CommandRequest): Promise<Command> {
    const now = Date.now();
    const commandId = request.commandId ?? createCommandId();
    const ttlMs = request.ttlMs ?? (config?.realtime?.commandTimeoutMs ?? 15_000) * 2;
    const targetDevice = dashboard.value.devices.find((device) => device.id === request.deviceId);
    const capability = targetDevice?.commandCapabilities?.[request.action];
    if (dataSource.value === 'BACKEND' && !capability) {
      throw new Error('当前设备未登记该控制能力，系统不会发送未注册命令。');
    }
    const requiredPermission = capability?.requiredPermission ?? 'command.issue';
    if (authEnabled.value && !can(requiredPermission)) {
      throw new Error('当前角色没有执行该控制命令的权限。');
    }
    const canonicalRequest: CommandRequest = { ...request, commandId, ttlMs, operator: currentUser.value?.id ?? request.operator };

    if (dataSource.value === 'BACKEND' && api) {
      try {
        const command = await api.issueCommand(canonicalRequest);
        upsertCommand(command);
        return command;
      } catch (cause) {
        const definiteReject = cause instanceof ApiError &&
          (cause.status !== null && ((cause.status >= 400 && cause.status < 500) || cause.status === 503)) &&
          !cause.outcomeUnknown;
        const observed: Command = {
          id: commandId,
          deviceId: request.deviceId,
          action: request.action,
          requestedValue: request.requestedValue,
          state: definiteReject ? 'REJECTED' : 'OUTCOME_UNKNOWN',
          issuedAt: now,
          updatedAt: now,
          expiresAt: now + ttlMs,
          operator: currentUser.value?.id ?? request.operator,
          reason: request.reason,
          detail: definiteReject
            ? `命令已被后端明确拒绝：${cause instanceof Error ? cause.message : String(cause)}`
            : `命令提交后的物理结果无法确认：${cause instanceof Error ? cause.message : String(cause)}`
        };
        upsertCommand(observed);
        return observed;
      }
    }

    const command: Command = {
      id: commandId,
      deviceId: request.deviceId,
      action: request.action,
      requestedValue: request.requestedValue,
      state: 'ISSUED',
      issuedAt: now,
      updatedAt: now,
      expiresAt: now + ttlMs,
      operator: request.operator ?? 'local-user',
      reason: request.reason ?? '本地操作'
    };
    upsertCommand(command);
    simulateDemoCommand(command.id);
    return command;
  }


  async function previewCommandV2(request: {deviceId:string;action:string;requestedValue?:unknown;reason:string}): Promise<CommandPreviewV2> {
    if (dataSource.value !== 'BACKEND') throw new Error('服务端操作预览仅适用于后端运行模式。');
    if (!config) config = await loadRuntimeConfig();
    if (!platformApi) platformApi = new PlatformApiClient(config);
    platformApi.setAccessToken(accessToken.value);
    return platformApi.previewCommand(request);
  }

  async function issuePreviewedCommandV2(preview: CommandPreviewV2, ttlMs?: number): Promise<Command> {
    if (dataSource.value !== 'BACKEND') throw new Error('服务端预览提交仅适用于后端运行模式。');
    if (!config) config = await loadRuntimeConfig();
    if (!platformApi) platformApi = new PlatformApiClient(config);
    platformApi.setAccessToken(accessToken.value);
    const commandId = createCommandId();
    const idempotencyKey = createIdempotencyKey();
    const now = Date.now();
    const effectiveTtlMs = ttlMs ?? (config.realtime?.commandTimeoutMs ?? 15_000) * 2;
    try {
      const command = validateCommand(await platformApi.issueCommand({
        previewId: preview.previewId,
        idempotencyKey,
        commandId,
        deviceId: preview.deviceId,
        action: preview.action,
        requestedValue: preview.requestedValue,
        reason: preview.reason,
        ttlMs: effectiveTtlMs
      }));
      upsertCommand(command);
      return command;
    } catch (cause) {
      const definiteReject = cause instanceof ApiError && cause.status !== null && cause.status >= 400 && cause.status < 500;
      if (definiteReject) throw cause;
      // The POST may have reached the backend. Never auto-resubmit. Query the
      // pre-generated commandId once; if the durable record is unavailable,
      // surface a local uncertainty observation tied to the same idempotency key.
      try {
        const command = validateCommand(await platformApi.getCommand(commandId));
        upsertCommand(command);
        return command;
      } catch {
        const observed: Command = {
          id: commandId,
          deviceId: preview.deviceId,
          action: preview.action,
          requestedValue: preview.requestedValue,
          state: 'OUTCOME_UNKNOWN',
          revision: 1,
          issuedAt: now,
          updatedAt: now,
          expiresAt: now + effectiveTtlMs,
          operator: currentUser.value?.id,
          reason: preview.reason,
          previewId: preview.previewId,
          idempotencyKey,
          configRevision: preview.configRevision,
          feedbackStatus: 'UNKNOWN',
          detail: `提交后的设备结果无法确认，系统未自动重发：${cause instanceof Error ? cause.message : String(cause)}`
        };
        upsertCommand(observed);
        return observed;
      }
    }
  }

  async function refreshCommandV2(commandId:string): Promise<Command> {
    if (!config) config = await loadRuntimeConfig();
    if (!platformApi) platformApi = new PlatformApiClient(config);
    platformApi.setAccessToken(accessToken.value);
    const command=validateCommand(await platformApi.getCommand(commandId));
    upsertCommand(command);
    return command;
  }

  async function reconcileCommandV2(commandId:string,input:CommandReconciliationInput): Promise<Command> {
    if (!can('command.reconcile')) throw new Error('当前角色没有命令核查权限。');
    if (!config) config = await loadRuntimeConfig();
    if (!platformApi) platformApi = new PlatformApiClient(config);
    platformApi.setAccessToken(accessToken.value);
    const command=validateCommand(await platformApi.reconcileCommand(commandId,input));
    upsertCommand(command);
    return command;
  }


  async function acknowledgeAlarm(alarmId: string, request: AlarmActionRequest = {}): Promise<Alarm> {
    if (authEnabled.value && !can('alarm.ack')) throw new Error('当前角色没有报警确认权限。');
    const canonicalRequest: AlarmActionRequest = { comment: request.comment?.trim() || undefined };
    if (dataSource.value === 'BACKEND' && api) {
      const alarm = validateAlarm(await api.acknowledgeAlarm(alarmId, canonicalRequest));
      upsertAlarm(alarm);
      return alarm;
    }
    const current = dashboard.value.alarms.find((a) => a.id === alarmId);
    if (!current) throw new Error(`Alarm not found: ${alarmId}`);
    if (current.state === 'CLEARED') throw new Error('已清除报警不能再确认。');
    const operatorId = currentUser.value?.id ?? request.operator ?? 'local-user';
    const alarm: Alarm = current.state === 'ACKNOWLEDGED' ? current : {
      ...current,
      state: 'ACKNOWLEDGED',
      acknowledgedAt: Date.now(),
      acknowledgedBy: operatorId,
      acknowledgementComment: canonicalRequest.comment
    };
    upsertAlarm(alarm);
    return alarm;
  }

  async function clearAlarm(alarmId: string, request: AlarmActionRequest): Promise<Alarm> {
    if (dataSource.value === 'BACKEND' && api) {
      const alarm = validateAlarm(await api.clearAlarm(alarmId, request));
      upsertAlarm(alarm);
      return alarm;
    }
    const current = dashboard.value.alarms.find((a) => a.id === alarmId);
    if (!current) throw new Error(`Alarm not found: ${alarmId}`);
    if (current.state !== 'CLEARED') {
      throw new Error('活动报警不能人工清除；CLEARED 只能由现场条件恢复后产生。');
    }
    return current;
  }

  async function getRuntimeStatus() {
    if (!api) {
      if (!config) config = await loadRuntimeConfig();
      api = new RestApiClient(config);
      platformApi = new PlatformApiClient(config);
      api.setAccessToken(accessToken.value);
    }
    return api.getRuntimeStatus();
  }

  async function queryTelemetryHistory(query: { deviceId?: string; pointId?: string; from?: number; to?: number; limit?: number }): Promise<TelemetryHistoryResponse> {
    if (dataSource.value === 'BACKEND' && api) return api.getTelemetryHistory(query);
    return { enabled:false, items:[], totalMatched:0, limit:query.limit ?? 2000, from:query.from ?? 0, to:query.to ?? Date.now(), truncated:false };
  }

  async function queryAlarmHistory(query: { deviceId?: string; alarmId?: string; from?: number; to?: number; limit?: number }): Promise<AlarmHistoryResponse> {
    if (dataSource.value === 'BACKEND' && api) return api.getAlarmHistory(query);
    return { enabled:false, items:[], totalMatched:0, limit:query.limit ?? 2000, from:query.from ?? 0, to:query.to ?? Date.now(), truncated:false };
  }

  async function queryAuditHistory(query: { actorId?: string; action?: string; resourceType?: string; from?: number; to?: number; limit?: number } = {}): Promise<AuditHistoryResponse> {
    if (authEnabled.value && !can('audit.view')) throw new Error('当前角色没有审计日志查看权限。');
    if (dataSource.value === 'BACKEND' && api) return api.getAuditHistory(query);
    return { enabled:false, items:[], totalMatched:0, limit:query.limit ?? 1000, from:query.from ?? 0, to:query.to ?? Date.now(), truncated:false };
  }



  async function refreshAgentStatus(): Promise<AgentStatusV2> {
    if (!config) config = await loadRuntimeConfig();
    if (!platformApi) platformApi = new PlatformApiClient(config);
    platformApi.setAccessToken(accessToken.value);
    const status = await platformApi.getAgentStatus();
    agentStatus.value = status;
    return status;
  }

  async function askAgent(message:string,history:Array<{role:'user'|'assistant';content:string}>=[]): Promise<AgentReplyV2> {
    if (!config) config = await loadRuntimeConfig();
    if (!platformApi) platformApi = new PlatformApiClient(config);
    platformApi.setAccessToken(accessToken.value);
    return platformApi.chatWithAgent(message.trim(),history);
  }

  async function refreshPlatformConfig(): Promise<void> {
    if (!config) config = await loadRuntimeConfig();
    if (!platformApi) platformApi = new PlatformApiClient(config);
    platformApi.setAccessToken(accessToken.value);
    const [status, assets, instances, rules, occurrences, incidents] = await Promise.all([
      platformApi.getConfigStatus(), platformApi.getAssets(), platformApi.getApplicationInstances(),
      platformApi.getRules(), platformApi.getAlarmOccurrences(), platformApi.getIncidents()
    ]);
    platformConfigStatus.value = status; platformAssets.value = assets; applicationInstances.value = instances;
    platformRules.value = rules; alarmOccurrencesV2.value = occurrences; incidentsV2.value = incidents;
  }

  async function getPlatformConfigSnapshot(): Promise<PlatformConfigSnapshot> {
    if (!config) config = await loadRuntimeConfig();
    if (!platformApi) platformApi = new PlatformApiClient(config);
    platformApi.setAccessToken(accessToken.value);
    return platformApi.getConfigSnapshot();
  }

  async function validatePlatformConfig(snapshot: PlatformConfigSnapshot): Promise<{draftId:string;validation:ConfigValidationResponse}> {
    if (authEnabled.value && !can('config.edit')) throw new Error('当前角色没有配置编辑权限。');
    if (!config) config = await loadRuntimeConfig();
    if (!platformApi) platformApi = new PlatformApiClient(config);
    platformApi.setAccessToken(accessToken.value);
    const baseRevision = platformConfigStatus.value?.publishedRevision;
    const draft = await platformApi.createDraft(snapshot, baseRevision);
    const validation = await platformApi.validateDraft(draft.draftId);
    return { draftId:draft.draftId, validation };
  }

  async function publishPlatformDraft(draftId:string, reason:string):Promise<ConfigPublishResponse> {
    if (authEnabled.value && !can('config.publish')) throw new Error('当前角色没有配置发布权限。');
    if (!config) config = await loadRuntimeConfig();
    if (!platformApi) platformApi = new PlatformApiClient(config);
    platformApi.setAccessToken(accessToken.value);
    const result=await platformApi.publishDraft(draftId,reason.trim());
    await refreshPlatformConfig();
    return result;
  }

  async function createValidatePublishConfig(snapshot: PlatformConfigSnapshot, reason: string): Promise<{validation:ConfigValidationResponse;publish?:ConfigPublishResponse}> {
    if (authEnabled.value && !can('config.publish')) throw new Error('当前角色没有配置发布权限。');
    if (!config) config = await loadRuntimeConfig();
    if (!platformApi) platformApi = new PlatformApiClient(config);
    platformApi.setAccessToken(accessToken.value);
    const baseRevision = platformConfigStatus.value?.publishedRevision;
    const draft = await platformApi.createDraft(snapshot, baseRevision);
    const validation = await platformApi.validateDraft(draft.draftId);
    if (!validation.valid) return { validation };
    const publish = await platformApi.publishDraft(draft.draftId, reason.trim());
    await refreshPlatformConfig();
    return { validation, publish };
  }

  async function refreshB3Operations():Promise<void> {
    if (!config) config = await loadRuntimeConfig();
    if (!platformApi) platformApi = new PlatformApiClient(config);
    platformApi.setAccessToken(accessToken.value);
    const [rules, occurrences, incidents] = await Promise.all([platformApi.getRules(), platformApi.getAlarmOccurrences(), platformApi.getIncidents()]);
    platformRules.value=rules; alarmOccurrencesV2.value=occurrences; incidentsV2.value=incidents;
  }

  async function acknowledgeAlarmOccurrenceV2(occurrenceId:string, comment?:string):Promise<AlarmOccurrenceV2> {
    if (authEnabled.value && !can('alarm.ack')) throw new Error('当前角色没有报警确认权限。');
    if (!config) config = await loadRuntimeConfig(); if (!platformApi) platformApi=new PlatformApiClient(config); platformApi.setAccessToken(accessToken.value);
    const result=await platformApi.acknowledgeAlarmOccurrence(occurrenceId,comment); await refreshB3Operations(); return result;
  }

  async function createIncidentV2(input:{occurrenceIds:string[];title?:string;priority?:string;assignee?:string;description?:string}):Promise<IncidentV2> {
    if (authEnabled.value && !can('incident.assign')) throw new Error('当前角色没有事件创建/分配权限。');
    if (!config) config = await loadRuntimeConfig(); if (!platformApi) platformApi=new PlatformApiClient(config); platformApi.setAccessToken(accessToken.value);
    const result=await platformApi.createIncident(input); await refreshB3Operations(); return result;
  }

  async function transitionIncidentV2(id:string,input:{action:'START'|'RESOLVE'|'CLOSE'|'REOPEN'|'ASSIGN';expectedRevision:number;comment?:string;assignee?:string}):Promise<IncidentV2> {
    if (!config) config = await loadRuntimeConfig(); if (!platformApi) platformApi=new PlatformApiClient(config); platformApi.setAccessToken(accessToken.value);
    const result=await platformApi.transitionIncident(id,input); await refreshB3Operations(); return result;
  }

  async function queryTimelineV2(query:{from?:number;to?:number;instanceId?:string}={}):Promise<TimelineResponseV2> {
    if (!config) config = await loadRuntimeConfig(); if (!platformApi) platformApi=new PlatformApiClient(config); platformApi.setAccessToken(accessToken.value); return platformApi.getTimeline(query);
  }

  async function queryPlatformTelemetryHistory(query:{deviceId?:string;pointId?:string;from?:number;to?:number;limit?:number}={}):Promise<PlatformTelemetryHistoryResponse> {
    if (!config) config = await loadRuntimeConfig(); if (!platformApi) platformApi=new PlatformApiClient(config); platformApi.setAccessToken(accessToken.value); return platformApi.getTelemetryHistory(query);
  }

  async function createReplaySessionV2(input:{instanceId:string;from:number;to:number;limit?:number;configRevision?:string}):Promise<ReplaySessionV2> {
    if (!config) config = await loadRuntimeConfig(); if (!platformApi) platformApi=new PlatformApiClient(config); platformApi.setAccessToken(accessToken.value); return platformApi.createReplaySession(input);
  }

  async function createReportJobV2(input:{instanceId:string;from:number;to:number;format?:string}):Promise<ReportJobV2> {
    if (authEnabled.value && !can('report.export')) throw new Error('当前角色没有运行报告导出权限。');
    if (!config) config = await loadRuntimeConfig(); if (!platformApi) platformApi=new PlatformApiClient(config); platformApi.setAccessToken(accessToken.value); return platformApi.createReportJob(input);
  }

  async function rollbackPlatformConfig(revision:string, reason:string):Promise<ConfigPublishResponse> {
    if (authEnabled.value && !can('config.publish')) throw new Error('当前角色没有配置回滚权限。');
    if (!config) config = await loadRuntimeConfig();
    if (!platformApi) platformApi = new PlatformApiClient(config);
    platformApi.setAccessToken(accessToken.value);
    const result = await platformApi.rollback(revision, reason.trim());
    await refreshPlatformConfig();
    return result;
  }

  function simulateDemoCommand(commandId: string): void {
    const steps: Array<{ delay: number; state: CommandEvent['state']; seq: number }> = [
      { delay: 250, state: 'RECEIVED', seq: 1 },
      { delay: 600, state: 'ACCEPTED', seq: 2 },
      { delay: 1000, state: 'EXECUTING', seq: 3 },
      { delay: 1500, state: 'APPLIED', seq: 4 },
      { delay: 2000, state: 'CONFIRMED', seq: 5 }
    ];
    steps.forEach((step) => setTimeout(() => applyCommandStatus({
      commandId,
      state: step.state,
      timestamp: Date.now(),
      seq: step.seq,
      detail: '设备操作状态更新'
    }), step.delay));
  }

  function applyFreshnessTick(now = Date.now()): void {
    const staleAfterMs = config?.realtime?.staleAfterMs ?? 10_000;
    const badAfterMs = config?.realtime?.badAfterMs ?? 60_000;
    dashboard.value = applyFreshness(dashboard.value, now, { staleAfterMs, badAfterMs });
    refreshScenarioCounters();
  }

  function applyCommandTimeouts(now = Date.now()): void {
    const timeoutMs = config?.realtime?.commandTimeoutMs ?? 15_000;
    if (dataSource.value === 'BACKEND') {
      // Backend command states are authoritative. The browser may report that a
      // view is stale, but it must not turn APPLIED/ACCEPTED into EXPIRED/TIMEOUT.
      commandObservations.value = Object.fromEntries(
        dashboard.value.commands.map((command) => [command.id, observeCommand(command, now, timeoutMs)])
      );
      return;
    }
    dashboard.value.commands = dashboard.value.commands.map((command) => {
      const expired = expireCommand(command, now);
      return expired.state === 'EXPIRED' ? expired : timeoutCommand(expired, now, timeoutMs);
    });
  }

  function startTimers(): void {
    if (freshnessTimer === null) freshnessTimer = setInterval(() => applyFreshnessTick(), 1_000);
    if (commandTimer === null) commandTimer = setInterval(() => applyCommandTimeouts(), 1_000);
  }

  function shutdown(): void {
    stopRealtime();
    if (freshnessTimer !== null) clearInterval(freshnessTimer);
    if (commandTimer !== null) clearInterval(commandTimer);
    freshnessTimer = null;
    commandTimer = null;
  }

  return {
    dashboard, dataSource, sourceMode, dataOrigin, dataOriginLabel, loading, error, fallbackReason,
    realtimeState, realtimeDiagnostics, lastRealtimeError,
    ignoredCommandEvents, duplicateCommandEvents, rejectedRealtimeMessages,
    ignoredTelemetryEvents, duplicateTelemetryEvents, commandObservations,
    authEnabled, authRequired, authError, currentUser, currentRole,
    platformConfigStatus, platformAssets, applicationInstances, platformRules, alarmOccurrencesV2, incidentsV2, agentStatus,
    onlineDevices, activeAlarms, degradedLinks, connectionLabel,
    useDemo, initialize, login, logout, can, retryBackend, getRuntimeStatus, issueCommand, previewCommandV2, issuePreviewedCommandV2, refreshCommandV2, reconcileCommandV2, acknowledgeAlarm, clearAlarm, queryTelemetryHistory, queryAlarmHistory, queryAuditHistory,
    refreshPlatformConfig, refreshB3Operations, refreshAgentStatus, askAgent, getPlatformConfigSnapshot, validatePlatformConfig, publishPlatformDraft, createValidatePublishConfig, rollbackPlatformConfig,
    acknowledgeAlarmOccurrenceV2, createIncidentV2, transitionIncidentV2, queryTimelineV2, queryPlatformTelemetryHistory, createReplaySessionV2, createReportJobV2,
    applyRealtimeMessage, applyFreshnessTick, applyCommandTimeouts, shutdown
  };
});
