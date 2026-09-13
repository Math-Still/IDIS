<script setup lang="ts">
defineProps<{ label:string; value:string|number; sub?:string; detail?:string; tone?:'info'|'live'|'good'|'warn'|'bad'|'neutral' }>();
</script>
<template>
  <article class="metric-card" :data-tone="tone ?? 'neutral'">
    <div class="metric-label">{{label}}</div>
    <div class="metric-value">{{value}}</div>
    <div v-if="sub || detail" class="metric-sub">{{sub || detail}}</div>
  </article>
</template>
