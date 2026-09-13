import { computed } from 'vue';
import { useSiteStore } from '../stores/site';

export function useDevice(deviceId: () => string) {
  const store = useSiteStore();
  const device = computed(() => store.dashboard.devices.find((d) => d.id === deviceId()));
  const telemetry = computed(() => store.dashboard.telemetry.filter((p) => p.deviceId === deviceId()));
  const alarms = computed(() => store.dashboard.alarms.filter((a) => a.deviceId === deviceId()));
  const communication = computed(() => store.dashboard.communication.find((c) => c.deviceId === deviceId()));
  return { device, telemetry, alarms, communication };
}
