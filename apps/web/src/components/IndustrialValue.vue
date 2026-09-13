<script setup lang="ts">
import { computed } from 'vue';
import type { TelemetryPoint } from '@smart-factory/domain';
import { formatTelemetry } from '../utils/industrial-format';

const props = defineProps<{ point?: TelemetryPoint | null; showUnit?: boolean }>();
const formatted = computed(() => formatTelemetry(props.point));
</script>

<template>
  <span class="industrial-reading" :data-value-status="formatted.status.toLowerCase()">
    <span class="industrial-reading-value">{{ formatted.valueText }}</span>
    <small v-if="(showUnit ?? true) && formatted.unitText" class="industrial-reading-unit">{{ formatted.unitText }}</small>
  </span>
</template>
