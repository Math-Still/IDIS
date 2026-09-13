import {
  BRIDGE_PROTOCOL_VERSION,
  type BridgeMethod,
  type BridgeRequest,
  type BridgeResponse,
  type NativeEvent
} from '@smart-factory/contracts';
import { PlatformError, type PlatformErrorCode, type PlatformErrorPayload } from '@smart-factory/shared';

export interface NativeBridgeProxy {
  invoke(requestJson: string): string;
}

declare global {
  interface Window {
    smartFactoryNative?: NativeBridgeProxy;
    __SMART_FACTORY_NATIVE_EVENT__?: (eventJson: string) => void;
  }
}

const MAX_BRIDGE_RESPONSE_CHARS = 256 * 1024;
const MAX_NATIVE_EVENT_CHARS = 128 * 1024;

function requestId(): string {
  return `web-${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}

const PLATFORM_ERROR_CODES = new Set<PlatformErrorCode>([
  'PLATFORM_UNSUPPORTED','CAPABILITY_UNSUPPORTED','PERMISSION_DENIED','NETWORK_UNAVAILABLE',
  'BRIDGE_TIMEOUT','BRIDGE_PROTOCOL_ERROR','NATIVE_ERROR','RESOURCE_LOAD_ERROR','VERSION_INCOMPATIBLE','UNKNOWN'
]);

function normalizePlatformError(value: unknown): PlatformErrorPayload {
  if (!isRecord(value) || typeof value.code !== 'string' || typeof value.message !== 'string' || typeof value.recoverable !== 'boolean') {
    return {
      code: 'BRIDGE_PROTOCOL_ERROR',
      message: 'Native bridge returned a malformed error payload.',
      details: value,
      recoverable: false
    };
  }
  return {
    code: PLATFORM_ERROR_CODES.has(value.code as PlatformErrorCode) ? value.code as PlatformErrorCode : 'UNKNOWN',
    message: value.message,
    details: value.details,
    recoverable: value.recoverable
  };
}

function normalizeNativeEvent(raw: unknown): NativeEvent<unknown> | null {
  if (!isRecord(raw)) return null;
  if (raw.type !== 'event' || typeof raw.eventId !== 'string' || !raw.eventId || typeof raw.event !== 'string' || !raw.event ||
      typeof raw.timestamp !== 'number' || !Number.isFinite(raw.timestamp) || raw.timestamp < 0 ||
      typeof raw.sequence !== 'number' || !Number.isInteger(raw.sequence) || raw.sequence < 0 ||
      typeof raw.source !== 'string' || !raw.source) return null;
  return raw as unknown as NativeEvent<unknown>;
}

class NativeEventMultiplexer {
  private handlers = new Set<(event: NativeEvent<unknown>) => void>();
  private seenIds = new Set<string>();
  private seenOrder: string[] = [];
  private installed = false;
  private readonly maxSeen = 256;

  subscribe(handler: (event: NativeEvent<unknown>) => void): () => void {
    this.ensureInstalled();
    this.handlers.add(handler);
    return () => this.handlers.delete(handler);
  }

  private ensureInstalled(): void {
    if (this.installed || typeof window === 'undefined') return;
    const previous = window.__SMART_FACTORY_NATIVE_EVENT__;
    window.__SMART_FACTORY_NATIVE_EVENT__ = (eventJson: string) => {
      if (previous) {
        try { previous(eventJson); } catch { /* isolate pre-existing handler */ }
      }
      this.dispatch(eventJson);
    };
    this.installed = true;
  }

  private dispatch(eventJson: string): void {
    if (typeof eventJson !== 'string' || eventJson.length > MAX_NATIVE_EVENT_CHARS) return;

    let event: NativeEvent<unknown> | null = null;
    try { event = normalizeNativeEvent(JSON.parse(eventJson)); }
    catch { event = null; }
    if (!event) return;

    if (this.seenIds.has(event.eventId)) return;
    this.seenIds.add(event.eventId);
    this.seenOrder.push(event.eventId);
    if (this.seenOrder.length > this.maxSeen) {
      const oldest = this.seenOrder.shift();
      if (oldest) this.seenIds.delete(oldest);
    }

    for (const handler of this.handlers) {
      try { handler(event); } catch { /* isolate subscriber failure */ }
    }
  }
}

const eventMux = new NativeEventMultiplexer();

export class NativeBridgeClient {
  constructor(private readonly timeoutMs = 3000) {}

  isAvailable(): boolean {
    return typeof window !== 'undefined' && typeof window.smartFactoryNative?.invoke === 'function';
  }

  async call<TResult = unknown, TParams = unknown>(
    method: BridgeMethod | string,
    params: TParams,
    timeoutMs = this.timeoutMs
  ): Promise<TResult> {
    if (!this.isAvailable()) {
      throw new PlatformError({
        code: 'PLATFORM_UNSUPPORTED',
        message: 'Native bridge proxy is not available.',
        recoverable: true
      });
    }

    const id = requestId();
    const req: BridgeRequest<TParams> = {
      bridgeVersion: BRIDGE_PROTOCOL_VERSION,
      id,
      method,
      params,
      timestamp: Date.now()
    };

    const startedAt = performance.now();
    let raw: string;
    try {
      raw = window.smartFactoryNative!.invoke(JSON.stringify(req));
    } catch (error) {
      throw new PlatformError({
        code: 'NATIVE_ERROR',
        message: 'Native bridge invocation failed.',
        details: String(error),
        recoverable: true
      });
    }

    const elapsedMs = performance.now() - startedAt;
    if (elapsedMs > timeoutMs) {
      throw new PlatformError({
        code: 'BRIDGE_TIMEOUT',
        message: `Native bridge call exceeded ${timeoutMs} ms: ${method}`,
        details: { method, elapsedMs },
        recoverable: true
      });
    }
    if (typeof raw !== 'string' || raw.length > MAX_BRIDGE_RESPONSE_CHARS) {
      throw new PlatformError({
        code: 'BRIDGE_PROTOCOL_ERROR',
        message: 'Native bridge response is missing or exceeds the maximum allowed size.',
        details: { method, chars: typeof raw === 'string' ? raw.length : null },
        recoverable: false
      });
    }

    let parsed: unknown;
    try { parsed = JSON.parse(raw); }
    catch (error) {
      throw new PlatformError({
        code: 'BRIDGE_PROTOCOL_ERROR',
        message: 'Native bridge returned invalid JSON.',
        details: { error: String(error) },
        recoverable: false
      });
    }

    if (!isRecord(parsed) || typeof parsed.bridgeVersion !== 'string' || typeof parsed.id !== 'string' || typeof parsed.ok !== 'boolean') {
      throw new PlatformError({
        code: 'BRIDGE_PROTOCOL_ERROR',
        message: 'Native bridge returned an invalid response envelope.',
        details: parsed,
        recoverable: false
      });
    }
    const response = parsed as unknown as BridgeResponse<TResult>;

    if (response.id !== id) {
      throw new PlatformError({
        code: 'BRIDGE_PROTOCOL_ERROR',
        message: 'Native bridge response id does not match request id.',
        details: { requestId: id, responseId: response.id, method },
        recoverable: false
      });
    }
    if (response.bridgeVersion !== BRIDGE_PROTOCOL_VERSION) {
      throw new PlatformError({
        code: 'VERSION_INCOMPATIBLE',
        message: `Bridge version mismatch: web=${BRIDGE_PROTOCOL_VERSION}, native=${response.bridgeVersion}`,
        recoverable: false
      });
    }

    if (!response.ok) throw new PlatformError(normalizePlatformError(response.error));
    if (!('result' in response)) {
      throw new PlatformError({
        code: 'BRIDGE_PROTOCOL_ERROR',
        message: 'Native bridge success response is missing result.',
        recoverable: false
      });
    }
    return response.result;
  }

  onEvent(handler: (event: NativeEvent<unknown>) => void): () => void {
    return eventMux.subscribe(handler);
  }
}
