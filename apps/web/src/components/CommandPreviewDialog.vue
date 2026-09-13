<script setup lang="ts">
import type { CommandPreviewV2 } from '@smart-factory/domain';
defineProps<{preview:CommandPreviewV2;busy?:boolean;error?:string|null}>();
const emit=defineEmits<{confirm:[];cancel:[]}>();
function valueText(value:unknown):string{return value===undefined||value===null?'无额外参数':typeof value==='object'?JSON.stringify(value):String(value);}
</script>
<template>
<div class="hmi-modal-backdrop" @click.self="!busy&&emit('cancel')">
  <section class="hmi-modal" role="dialog" aria-modal="true" aria-label="设备操作确认">
    <header><div><span>设备操作</span><strong>{{preview.displayName}}</strong></div><button class="modal-close" type="button" :disabled="busy" @click="emit('cancel')">×</button></header>
    <dl class="command-preview-grid">
      <div><dt>目标设备</dt><dd>{{preview.deviceName}}</dd></div>
      <div><dt>当前状态</dt><dd>{{preview.deviceStatus}}</dd></div>
      <div><dt>操作参数</dt><dd>{{valueText(preview.requestedValue)}}</dd></div>
      <div><dt>操作原因</dt><dd>{{preview.reason}}</dd></div>
    </dl>
    <div v-if="!preview.adapterWriteReady" class="inline-alert" data-state="bad">当前设备暂不可执行此操作。</div>
    <div v-if="error" class="inline-alert" data-state="bad">{{error}}</div>
    <footer><button class="btn-secondary" type="button" :disabled="busy" @click="emit('cancel')">取消</button><button class="btn-primary" type="button" :disabled="busy||!preview.adapterWriteReady" @click="emit('confirm')">{{busy?'提交中':'确认操作'}}</button></footer>
  </section>
</div>
</template>
<style scoped>
.command-preview-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:1px;margin:12px;background:var(--border-soft)}
.command-preview-grid>div{min-width:0;padding:10px;background:var(--surface-1)}.command-preview-grid dt{color:var(--text-4);font-size:12px}.command-preview-grid dd{margin:4px 0 0;color:var(--text-1);overflow-wrap:anywhere}@media(max-width:640px){.command-preview-grid{grid-template-columns:1fr}}
</style>
