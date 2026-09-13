import { afterEach, describe, expect, it } from 'vitest';
import { NativeBridgeClient } from '../src/native-bridge';
import { OpenHarmonyNativeAdapter } from '../src/openharmony-native';

const original = window.smartFactoryNative;

afterEach(() => {
  window.smartFactoryNative = original;
});

describe('NativeBridgeClient', () => {
  it('parses the string-only RPC response', async () => {
    window.smartFactoryNative = {
      invoke(requestJson: string): string {
        const req = JSON.parse(requestJson);
        return JSON.stringify({ bridgeVersion:'1.0.0', id:req.id, ok:true, result:['runtime.info'], error:null });
      }
    };
    const result = await new NativeBridgeClient().call<string[]>('capability.list', {});
    expect(result).toEqual(['runtime.info']);
  });

  it('rejects response correlation mismatch', async () => {
    window.smartFactoryNative = {
      invoke(): string {
        return JSON.stringify({ bridgeVersion:'1.0.0', id:'wrong-id', ok:true, result:{}, error:null });
      }
    };
    await expect(new NativeBridgeClient().call('bridge.health', {})).rejects.toMatchObject({ code:'BRIDGE_PROTOCOL_ERROR' });
  });

  it('lets the native adapter invoke a capability through the frozen adapter boundary', async () => {
    window.smartFactoryNative = {
      invoke(requestJson: string): string {
        const req = JSON.parse(requestJson);
        const result = req.method === 'capability.list'
          ? ['runtime.info','bridge.health','native.invoke','softbus.discovery']
          : req.method === 'softbus.status'
            ? { supported:true, provider:'test', initialized:true, watching:false, permission:'test', lastError:null, deviceCount:0 }
            : req.method === 'bridge.health'
              ? { adapterReady:true, bridgeReady:true, networkOnline:true, lastError:null }
              : {};
        return JSON.stringify({ bridgeVersion:'1.0.0', id:req.id, ok:true, result, error:null });
      }
    };

    const adapter = new OpenHarmonyNativeAdapter();
    expect(await adapter.hasCapability('softbus.discovery')).toBe(true);
    expect(await adapter.invokeNativeCapability<{supported:boolean}>('softbus.status', {})).toMatchObject({ supported:true });
  });

  it('deduplicates native events by eventId', () => {
    const client = new NativeBridgeClient();
    const events: unknown[] = [];
    const off = client.onEvent((event) => events.push(event));
    const payload = JSON.stringify({
      type:'event', eventId:'dedupe-case-1', event:'softbus.deviceStateChanged',
      data:{}, timestamp:Date.now(), sequence:1, source:'test'
    });
    window.__SMART_FACTORY_NATIVE_EVENT__?.(payload);
    window.__SMART_FACTORY_NATIVE_EVENT__?.(payload);
    off();
    expect(events).toHaveLength(1);
  });
});
