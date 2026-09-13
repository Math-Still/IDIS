<script setup lang="ts">
import PageHeader from '../components/PageHeader.vue';
import AppIcon from '../components/AppIcon.vue';
const groups=[
  {title:'设备与接入',items:[{to:'/devices',label:'设备台账',desc:'设备状态、位置与详情'},{to:'/communication',label:'工业通信',desc:'协议、链路质量与延迟'},{to:'/configuration',label:'设备与应用配置',desc:'资产、应用和平台配置'}]},
  {title:'规则与审计',items:[{to:'/rules',label:'规则管理',desc:'告警条件与联动规则'},{to:'/audit',label:'操作记录',desc:'操作、身份与审计追溯'}]},
  {title:'运行维护',items:[{to:'/diagnostics',label:'系统维护',desc:'运行诊断与维护入口'},{to:'/native',label:'系统接口',desc:'国产 OS 与原生能力边界'},{to:'/compatibility',label:'系统适配',desc:'兼容与目标环境适配'},{to:'/resilience',label:'恢复管理',desc:'恢复与韧性能力'},{to:'/benchmark',label:'性能监测',desc:'性能与运行指标'}]}
];
</script>
<template>
<section class="operator-page system-center-page"><PageHeader title="系统管理" eyebrow="配置 · 通信 · 规则 · 维护" description="把原有工程管理页面收敛到统一系统管理入口；原路由和功能保持不变。"/>
  <section v-for="group in groups" :key="group.title" class="system-center-group"><div class="domain-section-intro"><div><span>系统管理</span><strong>{{group.title}}</strong></div></div><div class="system-center-grid"><RouterLink v-for="item in group.items" :key="item.to" :to="item.to" class="system-center-card hmi-panel"><span><AppIcon name="diagnostics" :size="19"/></span><div><strong>{{item.label}}</strong><p>{{item.desc}}</p></div><b>进入 →</b></RouterLink></div></section>
</section>
</template>
