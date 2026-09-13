<script setup lang="ts">
import { ref } from 'vue';
import { useSiteStore } from '../stores/site';
const site=useSiteStore();
const userId=ref('operator');
const token=ref('');
const show=ref(false);
async function submit(){if(!userId.value.trim()||!token.value)return;await site.login(userId.value,token.value);}
</script>
<template>
<div class="login-gate">
  <section class="login-panel" aria-labelledby="login-title">
    <header><span>操作员身份认证</span><h1 id="login-title">智慧工厂安全监测控制平台</h1><p>控制与报警操作必须绑定经过认证的操作员身份。</p></header>
    <label class="form-field"><span>用户标识</span><input v-model="userId" autocomplete="username" @keyup.enter="submit"/></label>
    <label class="form-field"><span>访问令牌</span><div class="secret-field"><input v-model="token" :type="show?'text':'password'" autocomplete="current-password" @keyup.enter="submit"/><button type="button" class="btn-secondary" @click="show=!show">{{show?'隐藏':'显示'}}</button></div></label>
    <div v-if="site.authError" class="inline-alert" data-state="bad">{{site.authError}}</div>
    <button type="button" class="btn-primary login-submit" :disabled="site.loading||!userId.trim()||!token" @click="submit">{{site.loading?'正在认证':'登录系统'}}</button>
    <footer><span>分级权限控制已启用</span><span>关键操作将写入审计日志</span></footer>
  </section>
</div>
</template>
