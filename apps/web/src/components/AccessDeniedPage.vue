<script setup lang="ts">
import PageHeader from './PageHeader.vue';
defineProps<{ title?: string; reason?: string }>();
</script>

<template>
  <section class="operator-page access-denied-page">
    <PageHeader
      :title="title ?? '当前页面不可用'"
      eyebrow="访问控制"
      back-to="/"
      back-label="返回工业总览"
      :description="reason ?? '当前操作员没有访问此功能的权限。'"
    />
    <section class="hmi-panel access-denied-panel">
      <div class="access-denied-symbol" aria-hidden="true">!</div>
      <div>
        <strong>访问已限制</strong>
        <p>该功能仅向具备相应权限的人员开放。系统未加载受限页面内容。</p>
      </div>
      <RouterLink class="btn-primary link-button" to="/">返回工业总览</RouterLink>
    </section>
  </section>
</template>
