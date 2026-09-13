import { BrowserPlatformAdapter } from './browser';
import { OpenHarmonyNativeAdapter } from './openharmony-native';
import { NativeBridgeClient } from './native-bridge';
import type { PlatformAdapter } from './types';

export type PlatformAdapterMode = 'auto' | 'browser' | 'openharmony-native';

export function createPlatformAdapter(mode: PlatformAdapterMode = 'auto'): PlatformAdapter {
  if (mode === 'openharmony-native') return new OpenHarmonyNativeAdapter();
  if (mode === 'browser') return new BrowserPlatformAdapter();
  return new NativeBridgeClient().isAvailable()
    ? new OpenHarmonyNativeAdapter()
    : new BrowserPlatformAdapter();
}
