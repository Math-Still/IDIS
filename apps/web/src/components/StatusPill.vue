<script setup lang="ts">
const props = defineProps<{ tone?: 'info'|'live'|'good'|'warn'|'bad'|'muted'; label: string }>();
</script>
<template><span class="status-pill" :class="`tone-${props.tone ?? 'muted'}`"><span class="status-dot"></span><span>{{ label }}</span></span></template>
