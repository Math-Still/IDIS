import type { RuntimeConfig } from '@smart-factory/contracts';

let cached: RuntimeConfig | null = null;

function isLoopback(url: string): boolean {
  try {
    const host = new URL(url).hostname;
    return host === '127.0.0.1' || host === 'localhost' || host === '::1';
  } catch {
    return false;
  }
}

function positiveFinite(value: unknown, label: string, min = 1): void {
  if (value === undefined) return;
  if (typeof value !== 'number' || !Number.isFinite(value) || value < min) {
    throw new Error(`${label} must be a finite number >= ${min}.`);
  }
}

function ratio(value: unknown, label: string): void {
  if (value === undefined) return;
  if (typeof value !== 'number' || !Number.isFinite(value) || value < 0 || value > 1) {
    throw new Error(`${label} must be between 0 and 1.`);
  }
}

export function validateRuntimeConfig(value: unknown): RuntimeConfig {
  if (!value || typeof value !== 'object') throw new Error('runtime-config.json must contain an object.');
  const c = value as Partial<RuntimeConfig>;

  for (const key of ['apiBaseUrl', 'wsBaseUrl', 'environment', 'siteId', 'backendApiVersion'] as const) {
    if (typeof c[key] !== 'string' || !c[key]) throw new Error(`runtime-config.json missing required string: ${key}`);
  }
  if (!/^https?:\/\//i.test(c.apiBaseUrl!)) throw new Error('apiBaseUrl must use http:// or https://.');
  if (!/^wss?:\/\//i.test(c.wsBaseUrl!)) throw new Error('wsBaseUrl must use ws:// or wss://.');

  if (c.dataMode !== undefined && !['auto', 'backend', 'demo'].includes(c.dataMode)) {
    throw new Error('dataMode must be auto, backend, or demo.');
  }

  if (c.realtime) {
    positiveFinite(c.realtime.staleAfterMs, 'realtime.staleAfterMs');
    positiveFinite(c.realtime.badAfterMs, 'realtime.badAfterMs');
    positiveFinite(c.realtime.reconnectBaseMs, 'realtime.reconnectBaseMs');
    positiveFinite(c.realtime.reconnectMaxMs, 'realtime.reconnectMaxMs');
    positiveFinite(c.realtime.commandTimeoutMs, 'realtime.commandTimeoutMs');
    positiveFinite(c.realtime.stableConnectionMs, 'realtime.stableConnectionMs', 0);
    positiveFinite(c.realtime.messageSilenceReconnectMs, 'realtime.messageSilenceReconnectMs', 0);
    ratio(c.realtime.reconnectJitterRatio, 'realtime.reconnectJitterRatio');
    if (c.realtime.staleAfterMs !== undefined && c.realtime.badAfterMs !== undefined &&
        c.realtime.badAfterMs <= c.realtime.staleAfterMs) {
      throw new Error('realtime.badAfterMs must be greater than realtime.staleAfterMs.');
    }
    if (c.realtime.reconnectBaseMs !== undefined && c.realtime.reconnectMaxMs !== undefined &&
        c.realtime.reconnectMaxMs < c.realtime.reconnectBaseMs) {
      throw new Error('realtime.reconnectMaxMs must be >= realtime.reconnectBaseMs.');
    }
  }

  if (c.transport) {
    positiveFinite(c.transport.httpRequestTimeoutMs, 'transport.httpRequestTimeoutMs');
    positiveFinite(c.transport.maxJsonResponseBytes, 'transport.maxJsonResponseBytes', 1024);
    positiveFinite(c.transport.maxRealtimeMessageBytes, 'transport.maxRealtimeMessageBytes', 1024);
    if (c.transport.allowPlaintextForIsolatedLab !== undefined && typeof c.transport.allowPlaintextForIsolatedLab !== 'boolean') throw new Error('transport.allowPlaintextForIsolatedLab must be boolean.');
  }

  if (c.benchmark) {
    positiveFinite(c.benchmark.iterations, 'benchmark.iterations');
    positiveFinite(c.benchmark.warmupIterations, 'benchmark.warmupIterations', 0);
    positiveFinite(c.benchmark.intervalMs, 'benchmark.intervalMs', 0);
    positiveFinite(c.benchmark.requestTimeoutMs, 'benchmark.requestTimeoutMs');
  }

  if (c.faultInjection?.enabled !== undefined && typeof c.faultInjection.enabled !== 'boolean') {
    throw new Error('faultInjection.enabled must be boolean.');
  }
  if (c.faultInjection?.allowInFactory !== undefined && typeof c.faultInjection.allowInFactory !== 'boolean') {
    throw new Error('faultInjection.allowInFactory must be boolean.');
  }

  if (c.environment === 'factory') {
    if (!c.transport?.allowPlaintextForIsolatedLab && (!c.apiBaseUrl!.toLowerCase().startsWith('https://') || !c.wsBaseUrl!.toLowerCase().startsWith('wss://'))) {
      throw new Error('factory runtime-config requires HTTPS/WSS. Set transport.allowPlaintextForIsolatedLab=true only for an explicitly isolated lab.');
    }
    if (isLoopback(c.apiBaseUrl!) || isLoopback(c.wsBaseUrl!)) {
      throw new Error('factory runtime-config must not use loopback backend addresses.');
    }
    if ((c.dataMode ?? 'backend') !== 'backend') {
      throw new Error('factory runtime-config must use dataMode="backend"; demo fallback is disabled for factory deployments.');
    }
    if (c.faultInjection?.allowInFactory === true) {
      throw new Error('factory runtime-config must not enable faultInjection.allowInFactory.');
    }
    if (!c.deployment?.deploymentId || !c.deployment?.platformTarget || !c.deployment?.hardwareProfile) {
      throw new Error('factory runtime-config requires deploymentId, platformTarget, and hardwareProfile.');
    }
  }

  return c as RuntimeConfig;
}

export async function loadRuntimeConfig(): Promise<RuntimeConfig> {
  if (cached) return cached;
  const configUrl = new URL('./runtime-config.json', document.baseURI).toString();
  const response = await fetch(configUrl, { cache: 'no-store' });
  if (!response.ok) throw new Error(`runtime-config.json load failed: ${response.status}`);
  cached = validateRuntimeConfig(await response.json());
  return cached;
}
