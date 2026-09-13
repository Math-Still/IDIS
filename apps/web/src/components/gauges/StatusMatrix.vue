<script setup lang="ts">
withDefaults(defineProps<{
  items:Array<{key:string;label:string;state:string;value?:string|number}>;
  compact?:boolean;
}>(),{compact:false});
</script>

<template>
  <div class="industrial-status-matrix" :class="{'is-compact':compact}">
    <div v-for="item in items" :key="item.key" class="status-matrix-item" :data-state="item.state">
      <i></i><span>{{item.label}}</span><strong>{{item.value ?? '--'}}</strong>
    </div>
  </div>
</template>
