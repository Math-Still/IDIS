export type RealtimeState = 'IDLE' | 'CONNECTING' | 'CONNECTED' | 'RECONNECTING' | 'WAITING_NETWORK' | 'DISCONNECTED' | 'ERROR';

export interface RealtimeMessage<T = unknown> {
  type: string;
  data: T;
  timestamp: number;
  seq?: number;
}

export interface RealtimeDiagnostics {
  state: RealtimeState;
  reconnectCount: number;
  parseErrorCount: number;
  socketErrorCount: number;
  silenceTimeoutCount: number;
  rejectedFrameCount: number;
  subscriberErrorCount: number;
  lastConnectedAt: number | null;
  lastDisconnectedAt: number | null;
  lastMessageAt: number | null;
  nextReconnectDelayMs: number | null;
  lastCloseCode: number | null;
  lastCloseReason: string | null;
  networkOnline: boolean;
  backoffAttempt: number;
  stableSinceAt: number | null;
  lastStateChangeAt: number;
}

export interface RealtimeClient {
  readonly state: RealtimeState;
  connect(): void;
  disconnect(): void;
  reconnectNow(): void;
  onMessage(handler: (message: RealtimeMessage) => void): () => void;
  onState(handler: (state: RealtimeState) => void): () => void;
  getDiagnostics(): RealtimeDiagnostics;
}

export interface ReconnectPolicy {
  baseDelayMs: number;
  maxDelayMs: number;
  jitterRatio: number;
  stableConnectionMs?: number;
  messageSilenceReconnectMs?: number;
  maxMessageBytes?: number;
}

export const DEFAULT_RECONNECT_POLICY: ReconnectPolicy = {
  baseDelayMs: 1_000,
  maxDelayMs: 30_000,
  jitterRatio: 0.2,
  stableConnectionMs: 10_000,
  messageSilenceReconnectMs: 45_000,
  maxMessageBytes: 512 * 1024
};

export interface NetworkMonitor {
  isOnline(): boolean;
  subscribe(handler: (online: boolean) => void): () => void;
}

export class BrowserNetworkMonitor implements NetworkMonitor {
  isOnline(): boolean { return typeof navigator === 'undefined' ? true : navigator.onLine; }

  subscribe(handler: (online: boolean) => void): () => void {
    if (typeof window === 'undefined') return () => undefined;
    const on = () => handler(true);
    const off = () => handler(false);
    window.addEventListener('online', on);
    window.addEventListener('offline', off);
    return () => {
      window.removeEventListener('online', on);
      window.removeEventListener('offline', off);
    };
  }
}

type TimerHandle = ReturnType<typeof setTimeout>;

export class BrowserWebSocketClient implements RealtimeClient {
  private socket: WebSocket | null = null;
  private reconnectTimer: TimerHandle | null = null;
  private stableTimer: TimerHandle | null = null;
  private silenceTimer: TimerHandle | null = null;
  private messageHandlers = new Set<(message: RealtimeMessage) => void>();
  private stateHandlers = new Set<(state: RealtimeState) => void>();
  private _state: RealtimeState = 'IDLE';
  private manualStop = false;
  private attempt = 0;
  private unsubscribeNetwork: (() => void) | null = null;
  private diagnostics: RealtimeDiagnostics = {
    state: 'IDLE', reconnectCount: 0, parseErrorCount: 0, socketErrorCount: 0,
    silenceTimeoutCount: 0, rejectedFrameCount: 0, subscriberErrorCount: 0,
    lastConnectedAt: null, lastDisconnectedAt: null, lastMessageAt: null,
    nextReconnectDelayMs: null, lastCloseCode: null, lastCloseReason: null,
    networkOnline: true, backoffAttempt: 0, stableSinceAt: null,
    lastStateChangeAt: Date.now()
  };

  constructor(
    private readonly url: string,
    private readonly policy: ReconnectPolicy = DEFAULT_RECONNECT_POLICY,
    private readonly network: NetworkMonitor = new BrowserNetworkMonitor(),
    private readonly accessToken: string | null = null
  ) {}

  private socketUrl(): string {
    if (!this.accessToken) return this.url;
    const separator = this.url.includes('?') ? '&' : '?';
    return `${this.url}${separator}access_token=${encodeURIComponent(this.accessToken)}`;
  }

  get state(): RealtimeState { return this._state; }

  private setState(state: RealtimeState): void {
    if (this._state === state) return;
    this._state = state;
    this.diagnostics.state = state;
    this.diagnostics.lastStateChangeAt = Date.now();
    for (const handler of this.stateHandlers) {
      try { handler(state); } catch { this.diagnostics.subscriberErrorCount += 1; }
    }
  }

  private clearTimer(name: 'reconnect' | 'stable' | 'silence'): void {
    const timer = name === 'reconnect' ? this.reconnectTimer : name === 'stable' ? this.stableTimer : this.silenceTimer;
    if (timer !== null) clearTimeout(timer);
    if (name === 'reconnect') {
      this.reconnectTimer = null;
      this.diagnostics.nextReconnectDelayMs = null;
    } else if (name === 'stable') this.stableTimer = null;
    else this.silenceTimer = null;
  }

  private calculateDelay(): number {
    const exponential = Math.min(this.policy.maxDelayMs, this.policy.baseDelayMs * Math.pow(2, Math.max(0, this.attempt - 1)));
    const jitter = exponential * this.policy.jitterRatio;
    return Math.max(0, Math.round(exponential - jitter + Math.random() * jitter * 2));
  }

  private installNetworkMonitor(): void {
    if (this.unsubscribeNetwork) return;
    this.diagnostics.networkOnline = this.network.isOnline();
    this.unsubscribeNetwork = this.network.subscribe((online) => {
      this.diagnostics.networkOnline = online;
      if (!online) {
        this.clearTimer('reconnect');
        this.clearTimer('stable');
        this.clearTimer('silence');
        const current = this.socket;
        this.socket = null;
        try { current?.close(4002, 'network-offline'); } catch { /* no-op */ }
        if (!this.manualStop) this.setState('WAITING_NETWORK');
        return;
      }
      if (!this.manualStop && !this.socket) this.openSocket(true);
    });
  }

  connect(): void {
    this.manualStop = false;
    this.installNetworkMonitor();
    this.clearTimer('reconnect');
    if (!this.network.isOnline()) {
      this.diagnostics.networkOnline = false;
      this.setState('WAITING_NETWORK');
      return;
    }
    this.openSocket(false);
  }

  private scheduleStableReset(): void {
    this.clearTimer('stable');
    const ms = this.policy.stableConnectionMs ?? 10_000;
    if (ms <= 0) {
      this.attempt = 0;
      this.diagnostics.backoffAttempt = 0;
      this.diagnostics.stableSinceAt = Date.now();
      return;
    }
    this.stableTimer = setTimeout(() => {
      if (this._state === 'CONNECTED') {
        this.attempt = 0;
        this.diagnostics.backoffAttempt = 0;
        this.diagnostics.stableSinceAt = Date.now();
      }
    }, ms);
  }

  private resetSilenceWatchdog(): void {
    this.clearTimer('silence');
    const ms = this.policy.messageSilenceReconnectMs ?? 0;
    if (ms <= 0 || this._state !== 'CONNECTED') return;
    this.silenceTimer = setTimeout(() => {
      if (this._state !== 'CONNECTED' || this.manualStop) return;
      this.diagnostics.silenceTimeoutCount += 1;
      const current = this.socket;
      try { current?.close(4001, 'message-silence-timeout'); }
      catch {
        this.socket = null;
        this.scheduleReconnect();
      }
    }, ms);
  }

  private textBytes(value: string): number {
    return typeof TextEncoder !== 'undefined' ? new TextEncoder().encode(value).byteLength : value.length;
  }

  private handleMessage(event: MessageEvent, socket: WebSocket): void {
    if (socket !== this.socket) return;
    if (typeof event.data !== 'string') {
      this.diagnostics.rejectedFrameCount += 1;
      return;
    }

    const maxBytes = this.policy.maxMessageBytes ?? DEFAULT_RECONNECT_POLICY.maxMessageBytes!;
    if (this.textBytes(event.data) > maxBytes) {
      this.diagnostics.rejectedFrameCount += 1;
      return;
    }

    let parsed: RealtimeMessage;
    try {
      parsed = JSON.parse(event.data) as RealtimeMessage;
      if (!parsed || typeof parsed !== 'object' || typeof parsed.type !== 'string' || !('data' in parsed) ||
          !Number.isFinite(parsed.timestamp) || parsed.timestamp < 0) {
        throw new Error('Malformed realtime message');
      }
    } catch {
      this.diagnostics.parseErrorCount += 1;
      return;
    }

    // Only a structurally valid application message proves that the realtime
    // channel is alive. Invalid/binary frames must not keep a dead link green.
    this.diagnostics.lastMessageAt = Date.now();
    this.resetSilenceWatchdog();
    for (const handler of this.messageHandlers) {
      try { handler(parsed); } catch { this.diagnostics.subscriberErrorCount += 1; }
    }
  }

  private openSocket(isReconnect: boolean): void {
    if (this.socket || this._state === 'CONNECTING') return;
    if (!this.network.isOnline()) {
      this.setState('WAITING_NETWORK');
      return;
    }

    this.setState(isReconnect ? 'RECONNECTING' : 'CONNECTING');
    try {
      const socket = new WebSocket(this.socketUrl());
      this.socket = socket;
      socket.onopen = () => {
        if (socket !== this.socket) return;
        this.diagnostics.lastConnectedAt = Date.now();
        this.diagnostics.stableSinceAt = null;
        this.diagnostics.nextReconnectDelayMs = null;
        this.setState('CONNECTED');
        this.scheduleStableReset();
        this.resetSilenceWatchdog();
      };
      socket.onmessage = (event) => this.handleMessage(event, socket);
      socket.onerror = () => {
        if (socket !== this.socket) return;
        this.diagnostics.socketErrorCount += 1;
        this.setState('ERROR');
        try { socket.close(); }
        catch {
          this.socket = null;
          this.scheduleReconnect();
        }
      };
      socket.onclose = (event) => {
        if (socket !== this.socket) return;
        this.socket = null;
        this.clearTimer('stable');
        this.clearTimer('silence');
        this.diagnostics.lastDisconnectedAt = Date.now();
        this.diagnostics.lastCloseCode = event.code;
        this.diagnostics.lastCloseReason = event.reason || null;
        if (this.manualStop) {
          this.setState('DISCONNECTED');
          return;
        }
        if (!this.network.isOnline()) {
          this.setState('WAITING_NETWORK');
          return;
        }
        this.scheduleReconnect();
      };
    } catch {
      this.socket = null;
      this.diagnostics.socketErrorCount += 1;
      if (this.manualStop) this.setState('DISCONNECTED');
      else this.scheduleReconnect();
    }
  }

  private scheduleReconnect(): void {
    if (this.manualStop || this.reconnectTimer !== null) return;
    if (!this.network.isOnline()) {
      this.setState('WAITING_NETWORK');
      return;
    }
    this.attempt += 1;
    this.diagnostics.backoffAttempt = this.attempt;
    const delay = this.calculateDelay();
    this.diagnostics.reconnectCount += 1;
    this.diagnostics.nextReconnectDelayMs = delay;
    this.setState('RECONNECTING');
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.diagnostics.nextReconnectDelayMs = null;
      this.openSocket(true);
    }, delay);
  }

  reconnectNow(): void {
    if (this.manualStop) return;
    this.clearTimer('reconnect');
    this.clearTimer('stable');
    this.clearTimer('silence');
    this.attempt = 0;
    this.diagnostics.backoffAttempt = 0;
    const current = this.socket;
    this.socket = null;
    try { current?.close(4000, 'manual-reconnect'); } catch { /* no-op */ }
    if (this.network.isOnline()) this.openSocket(true);
    else this.setState('WAITING_NETWORK');
  }

  disconnect(): void {
    this.manualStop = true;
    this.clearTimer('reconnect');
    this.clearTimer('stable');
    this.clearTimer('silence');
    const current = this.socket;
    this.socket = null;
    try { current?.close(1000, 'manual-stop'); } catch { /* no-op */ }
    this.unsubscribeNetwork?.();
    this.unsubscribeNetwork = null;
    this.setState('DISCONNECTED');
  }

  onMessage(handler: (message: RealtimeMessage) => void): () => void {
    this.messageHandlers.add(handler);
    return () => this.messageHandlers.delete(handler);
  }

  onState(handler: (state: RealtimeState) => void): () => void {
    this.stateHandlers.add(handler);
    return () => this.stateHandlers.delete(handler);
  }

  getDiagnostics(): RealtimeDiagnostics {
    return { ...this.diagnostics, networkOnline: this.network.isOnline() };
  }
}
