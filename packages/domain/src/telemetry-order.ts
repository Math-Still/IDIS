import type { TelemetryPoint } from './models';

export type TelemetryOrderDecision = 'NEW' | 'DUPLICATE' | 'OUT_OF_ORDER';

export function classifyTelemetryOrder(
  current: Pick<TelemetryPoint,'sampleTs'|'seq'> | undefined,
  incoming: Pick<TelemetryPoint,'sampleTs'|'seq'>
): TelemetryOrderDecision {
  if (!current) return 'NEW';
  if (incoming.sampleTs < current.sampleTs) return 'OUT_OF_ORDER';
  if (incoming.sampleTs === current.sampleTs) {
    if (incoming.seq === current.seq) return 'DUPLICATE';
    if (incoming.seq < current.seq) return 'OUT_OF_ORDER';
  }
  // A newer physical timestamp is authoritative even if the device sequence
  // restarted after a reboot. Trend logic treats that sequence reset as a gap.
  return 'NEW';
}
