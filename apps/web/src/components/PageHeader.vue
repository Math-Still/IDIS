<script setup lang="ts">
import AppIcon from './AppIcon.vue';
defineProps<{ title: string; description?: string; eyebrow?: string; backTo?: string; backLabel?: string }>();
</script>

<template>
  <header class="page-header">
    <div class="page-header-copy">
      <RouterLink v-if="backTo" class="page-back-link" :to="backTo"><AppIcon name="chevronRight" :size="14" /><span>{{ backLabel ?? '返回' }}</span></RouterLink>
      <p v-if="eyebrow" class="page-kicker">{{ eyebrow }}</p>
      <h1>{{ title }}</h1>
      <p v-if="description" class="page-description">{{ description }}</p>
    </div>
    <div v-if="$slots.actions" class="page-actions"><slot name="actions" /></div>
  </header>
</template>
