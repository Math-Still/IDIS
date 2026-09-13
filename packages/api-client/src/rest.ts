import type { AuthStatusResponse, BackendRuntimeStatus, RuntimeConfig } from '@smart-factory/contracts';
import type { Alarm, AlarmActionRequest, AlarmHistoryResponse, AuditHistoryResponse, AuthSessionResponse, Command, CommandRequest, CommunicationHealth, DashboardSnapshot, Device, TelemetryHistoryResponse } from '@smart-factory/domain';

const DEFAULT_HTTP_TIMEOUT_MS = 8_000;
const DEFAULT_MAX_JSON_BYTES = 2 * 1024 * 1024;

export class ApiError extends Error {
  readonly status: number | null;
  readonly code: string | null;
  readonly details: unknown;
  readonly outcomeUnknown: boolean;
  constructor(message: string, options: { status?:number|null; code?:string|null; details?:unknown; outcomeUnknown?:boolean } = {}) {
    super(message);
    this.name = 'ApiError';
    this.status = options.status ?? null;
    this.code = options.code ?? null;
    this.details = options.details;
    this.outcomeUnknown = options.outcomeUnknown ?? false;
  }
}

export class RestApiClient {
  private accessToken: string | null = null;
  constructor(private readonly config: RuntimeConfig) {}

  setAccessToken(token: string | null): void { this.accessToken = token?.trim() || null; }
  getAccessToken(): string | null { return this.accessToken; }

  private url(path: string): string {
    return `${this.config.apiBaseUrl.replace(/\/$/, '')}/${this.config.backendApiVersion}/${path.replace(/^\//, '')}`;
  }

  private async request<T>(path: string, init: RequestInit = {}): Promise<T> {
    const timeoutMs = this.config.transport?.httpRequestTimeoutMs ?? DEFAULT_HTTP_TIMEOUT_MS;
    const maxBytes = this.config.transport?.maxJsonResponseBytes ?? DEFAULT_MAX_JSON_BYTES;
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);

    try {
      const response = await fetch(this.url(path), {
        ...init,
        signal: controller.signal,
        headers: {
          Accept: 'application/json',
          ...(init.body ? { 'Content-Type': 'application/json' } : {}),
          ...(this.accessToken ? { Authorization: `Bearer ${this.accessToken}` } : {}),
          ...(init.headers ?? {})
        }
      });

      if (!response.ok) {
        let payload: unknown = null;
        let message = `${init.method ?? 'GET'} ${path} failed: ${response.status}`;
        let code: string | null = null;
        try {
          const text = await response.text();
          payload = text ? JSON.parse(text) : null;
          if (payload && typeof payload === 'object') {
            const body = payload as Record<string, unknown>;
            if (typeof body.error === 'string' && body.error) message = body.error;
            if (typeof body.code === 'string' && body.code) code = body.code;
          }
        } catch { /* preserve HTTP status even when the body is not JSON */ }
        throw new ApiError(message, { status:response.status, code, details:payload, outcomeUnknown:false });
      }
      const declaredLength = Number(response.headers.get('content-length') ?? '0');
      if (Number.isFinite(declaredLength) && declaredLength > maxBytes) {
        throw new Error(`${init.method ?? 'GET'} ${path} response exceeds ${maxBytes} bytes`);
      }

      const text = await response.text();
      const bytes = typeof TextEncoder !== 'undefined' ? new TextEncoder().encode(text).byteLength : text.length;
      if (bytes > maxBytes) throw new Error(`${init.method ?? 'GET'} ${path} response exceeds ${maxBytes} bytes`);
      try {
        return JSON.parse(text) as T;
      } catch {
        throw new Error(`${init.method ?? 'GET'} ${path} returned invalid JSON`);
      }
    } catch (error) {
      if (error instanceof Error && error.name === 'AbortError') {
        throw new ApiError(`${init.method ?? 'GET'} ${path} timed out after ${timeoutMs} ms`, { outcomeUnknown:(init.method ?? 'GET').toUpperCase() !== 'GET' });
      }
      throw error;
    } finally {
      clearTimeout(timer);
    }
  }

  getAuthStatus(): Promise<AuthStatusResponse> { return this.request<AuthStatusResponse>('auth/status'); }

  login(userId: string, token: string): Promise<AuthSessionResponse> {
    return this.request<AuthSessionResponse>('auth/login', { method: 'POST', body: JSON.stringify({ userId, token }) });
  }
  getMe(): Promise<AuthSessionResponse> { return this.request<AuthSessionResponse>('auth/me'); }

  getDashboard(): Promise<DashboardSnapshot> { return this.request<DashboardSnapshot>('dashboard'); }
  getDevices(): Promise<Device[]> { return this.request<Device[]>('devices'); }
  getAlarms(): Promise<Alarm[]> { return this.request<Alarm[]>('alarms'); }
  getCommunicationHealth(): Promise<CommunicationHealth[]> { return this.request<CommunicationHealth[]>('communication/health'); }
  getRuntimeStatus(): Promise<BackendRuntimeStatus> { return this.request<BackendRuntimeStatus>('runtime/status'); }

  getTelemetryHistory(query: { deviceId?: string; pointId?: string; from?: number; to?: number; limit?: number } = {}): Promise<TelemetryHistoryResponse> {
    const params = new URLSearchParams();
    if (query.deviceId) params.set('deviceId', query.deviceId);
    if (query.pointId) params.set('pointId', query.pointId);
    if (query.from !== undefined) params.set('from', String(query.from));
    if (query.to !== undefined) params.set('to', String(query.to));
    if (query.limit !== undefined) params.set('limit', String(query.limit));
    const suffix = params.toString();
    return this.request<TelemetryHistoryResponse>(`history/telemetry${suffix ? `?${suffix}` : ''}`);
  }

  getAlarmHistory(query: { deviceId?: string; alarmId?: string; from?: number; to?: number; limit?: number } = {}): Promise<AlarmHistoryResponse> {
    const params = new URLSearchParams();
    if (query.deviceId) params.set('deviceId', query.deviceId);
    if (query.alarmId) params.set('alarmId', query.alarmId);
    if (query.from !== undefined) params.set('from', String(query.from));
    if (query.to !== undefined) params.set('to', String(query.to));
    if (query.limit !== undefined) params.set('limit', String(query.limit));
    const suffix = params.toString();
    return this.request<AlarmHistoryResponse>(`history/alarms${suffix ? `?${suffix}` : ''}`);
  }

  acknowledgeAlarm(alarmId: string, request: AlarmActionRequest): Promise<Alarm> {
    return this.request<Alarm>(`alarms/${encodeURIComponent(alarmId)}/ack`, { method: 'POST', body: JSON.stringify(request) });
  }

  clearAlarm(alarmId: string, request: AlarmActionRequest): Promise<Alarm> {
    return this.request<Alarm>(`alarms/${encodeURIComponent(alarmId)}/clear`, { method: 'POST', body: JSON.stringify(request) });
  }

  getAuditHistory(query: { actorId?: string; action?: string; resourceType?: string; from?: number; to?: number; limit?: number } = {}): Promise<AuditHistoryResponse> {
    const params = new URLSearchParams();
    if (query.actorId) params.set('actorId', query.actorId);
    if (query.action) params.set('action', query.action);
    if (query.resourceType) params.set('resourceType', query.resourceType);
    if (query.from !== undefined) params.set('from', String(query.from));
    if (query.to !== undefined) params.set('to', String(query.to));
    if (query.limit !== undefined) params.set('limit', String(query.limit));
    const suffix = params.toString();
    return this.request<AuditHistoryResponse>(`audit${suffix ? `?${suffix}` : ''}`);
  }

  async issueCommand(request: CommandRequest): Promise<Command> {
    if (!request.commandId) throw new Error('CommandRequest.commandId is required for idempotent delivery.');
    const command = await this.request<Command>('commands', { method: 'POST', body: JSON.stringify(request) });
    if (command.id !== request.commandId) {
      throw new Error(`Command response id mismatch: request=${request.commandId}, response=${command.id}`);
    }
    return command;
  }
}
