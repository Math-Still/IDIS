<script setup lang="ts">
const props = defineProps<{ name: string; size?: number }>();

const paths: Record<string, string[]> = {
  overview: ['M4 4h6v6H4z M14 4h6v10h-6z M4 14h6v6H4z M14 18h6v2h-6z'],
  monitor: ['M3 4h18v13H3z M8 21h8 M12 17v4 M6 12l3-3 3 2 4-5 2 2'],
  scenarios: ['M4 4h6v6H4z M14 4h6v6h-6z M4 14h6v6H4z M14 14h6v6h-6z'],
  devices: ['M8 3v3 M16 3v3 M8 18v3 M16 18v3 M3 8h3 M3 16h3 M18 8h3 M18 16h3 M7 7h10v10H7z'],
  alarms: ['M6 9a6 6 0 0 1 12 0c0 7 3 7 3 8H3c0-1 3-1 3-8 M10 21h4'],
  communication: ['M5 12a10 10 0 0 1 14 0 M8 15a6 6 0 0 1 8 0 M11 18a2 2 0 0 1 2 0 M12 21h.01'],
  native: ['M8 9l-4 3 4 3 M16 9l4 3-4 3 M14 5l-4 14'],
  compatibility: ['M12 3l8 4-8 4-8-4 8-4z M4 12l8 4 8-4 M4 17l8 4 8-4'],
  resilience: ['M12 3l7 3v5c0 5-3 8-7 10-4-2-7-5-7-10V6l7-3z M9 12l2 2 4-4'],
  benchmark: ['M4 20V10 M10 20V4 M16 20v-7 M22 20H2'],
  diagnostics: ['M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8z M12 2v2 M12 20v2 M4.9 4.9l1.4 1.4 M17.7 17.7l1.4 1.4 M2 12h2 M20 12h2 M4.9 19.1l1.4-1.4 M17.7 6.3l1.4-1.4'],
  search: ['M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14z M20 20l-4-4'],
  external: ['M14 4h6v6 M20 4l-9 9 M18 13v6H5V6h6'],
  chevronRight: ['M9 5l7 7-7 7'],
  refresh: ['M20 6v6h-6 M4 18v-6h6 M18 9a7 7 0 0 0-12-3 M6 15a7 7 0 0 0 12 3'],
  environment: ['M12 3v10 M8 9h4 M8 17a4 4 0 1 0 8 0c0-2-1-3-2-4V3h-4v10c-1 1-2 2-2 4z'],
  lighting: ['M9 18h6 M10 22h4 M8 13a6 6 0 1 1 8 0c-1 1-2 2-2 4h-4c0-2-1-3-2-4z'],
  gas: ['M5 18h14 M7 18V9l5-4 5 4v9 M10 13h4 M12 11v4'],
  agv: ['M4 7h13l3 5v5H4z M7 17a2 2 0 1 0 0 4 2 2 0 0 0 0-4z M17 17a2 2 0 1 0 0 4 2 2 0 0 0 0-4z M17 9h2'],
  count: ['M5 4h14v16H5z M8 8h8 M8 12h5 M8 16h3'],
  shield: ['M12 3l7 3v5c0 5-3 8-7 10-4-2-7-5-7-10V6l7-3z'],
  network: ['M4 7h16 M7 4v6 M17 4v6 M4 17h16 M7 14v6 M17 14v6'],
  system: ['M6 5h12v14H6z M9 9h6 M9 13h6'],
  warning: ['M12 3l10 18H2L12 3z M12 9v5 M12 18h.01'],
  history: ['M12 8v5l3 2 M3 12a9 9 0 1 0 3-6.7 M3 4v5h5']
};
</script>

<template>
  <svg
    class="app-icon"
    :width="props.size ?? 18"
    :height="props.size ?? 18"
    viewBox="0 0 24 24"
    fill="none"
    stroke="currentColor"
    stroke-width="1.7"
    stroke-linecap="round"
    stroke-linejoin="round"
    aria-hidden="true"
  >
    <path v-for="d in (paths[props.name] ?? paths.overview)" :key="d" :d="d" />
  </svg>
</template>
