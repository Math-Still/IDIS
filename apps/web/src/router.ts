import { createRouter, createWebHashHistory } from 'vue-router';
import OverviewPage from './views/OverviewPage.vue';
import WorkbenchPage from './views/WorkbenchPage.vue';
import ApplicationsPage from './views/ApplicationsPage.vue';
import ApplicationHistoryPage from './views/ApplicationHistoryPage.vue';
import ReplayPage from './views/ReplayPage.vue';
import ReportPage from './views/ReportPage.vue';
import RealtimeMonitorPage from './views/RealtimeMonitorPage.vue';
import ScenariosPage from './views/ScenariosPage.vue';
import ScenarioDetailPage from './views/ScenarioDetailPage.vue';
import DevicesPage from './views/DevicesPage.vue';
import DeviceDetailPage from './views/DeviceDetailPage.vue';
import AlarmsPage from './views/AlarmsPage.vue';
import CommunicationPage from './views/CommunicationPage.vue';
import HistoryPage from './views/HistoryPage.vue';
import NativeCapabilitiesPage from './views/NativeCapabilitiesPage.vue';
import DiagnosticsPage from './views/DiagnosticsPage.vue';
import CompatibilityPage from './views/CompatibilityPage.vue';
import ResiliencePage from './views/ResiliencePage.vue';
import BenchmarkPage from './views/BenchmarkPage.vue';
import AuditPage from './views/AuditPage.vue';
import ApplicationInstancePage from './views/ApplicationInstancePage.vue';
import ConfigurationPage from './views/ConfigurationPage.vue';
import RuleManagementPage from './views/RuleManagementPage.vue';
import IncidentDetailPage from './views/IncidentDetailPage.vue';
import AgentPage from './views/AgentPage.vue';
import CapabilityWorkspacePage from './views/CapabilityWorkspacePage.vue';
import KnowledgeCenterPage from './views/KnowledgeCenterPage.vue';
import EcosystemPage from './views/EcosystemPage.vue';
import SystemCenterPage from './views/SystemCenterPage.vue';

export const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    { path:'/', component:WorkbenchPage, meta:{ title:'综合总览' } },
    { path:'/safety', component:CapabilityWorkspacePage, meta:{ title:'安全监测', workspace:'safety' } },
    { path:'/operations', component:CapabilityWorkspacePage, meta:{ title:'生产运营', workspace:'operations' } },
    { path:'/asset-energy', component:CapabilityWorkspacePage, meta:{ title:'设备与能源', workspace:'assets' } },
    { path:'/intelligence', component:CapabilityWorkspacePage, meta:{ title:'智能分析', workspace:'intelligence' } },
    { path:'/control', component:CapabilityWorkspacePage, meta:{ title:'可视化与控制', workspace:'control' } },
    { path:'/knowledge', component:KnowledgeCenterPage, meta:{ title:'知识中心' } },
    { path:'/ecosystem', component:EcosystemPage, meta:{ title:'应用生态' } },
    { path:'/system', component:SystemCenterPage, meta:{ title:'系统管理', engineering:true } },
    { path:'/overview', component:OverviewPage, meta:{ title:'现场总览' } },
    { path:'/screen', component:RealtimeMonitorPage, meta:{ title:'运行大屏', layout:'monitor' } },
    { path:'/monitor', redirect:'/screen' },
    { path:'/assistant', component:AgentPage, meta:{ title:'生产助手' } },
    { path:'/applications', component:ApplicationsPage, meta:{ title:'应用监测' } },
    { path:'/scenarios', redirect:'/applications' },
    { path:'/scenarios/:type', component:ScenarioDetailPage, meta:{ title:'安全监测详情' } },
    { path:'/applications/:id', component:ApplicationInstancePage, meta:{ title:'监测详情' } },
    { path:'/applications/:id/history', component:ApplicationHistoryPage, meta:{ title:'应用历史回溯' } },
    { path:'/applications/:id/replay', component:ReplayPage, meta:{ title:'历史回放' } },
    { path:'/reports', component:ReportPage, meta:{ title:'运行报告', permission:'report.export' } },
    { path:'/devices', component:DevicesPage, meta:{ title:'设备与感知' } },
    { path:'/devices/:id', component:DeviceDetailPage, meta:{ title:'设备详情' } },
    { path:'/alarms', component:AlarmsPage, meta:{ title:'报警事件' } },
    { path:'/incidents/:id?', component:IncidentDetailPage, meta:{ title:'报警与处置' } },
    { path:'/communication', component:CommunicationPage, meta:{ title:'工业通信' } },
    { path:'/history', component:HistoryPage, meta:{ title:'历史趋势' } },
    { path:'/audit', component:AuditPage, meta:{ title:'操作记录', permission:'audit.view' } },
    { path:'/configuration', component:ConfigurationPage, meta:{ title:'设备与应用配置', engineering:true, permission:'config.edit' } },
    { path:'/rules', component:RuleManagementPage, meta:{ title:'规则管理', engineering:true, permission:'rule.manage' } },
    { path:'/diagnostics', component:DiagnosticsPage, meta:{ title:'系统维护', engineering:true } },
    { path:'/native', component:NativeCapabilitiesPage, meta:{ title:'系统接口', engineering:true } },
    { path:'/compatibility', component:CompatibilityPage, meta:{ title:'系统适配', engineering:true } },
    { path:'/resilience', component:ResiliencePage, meta:{ title:'恢复管理', engineering:true } },
    { path:'/benchmark', component:BenchmarkPage, meta:{ title:'性能监测', engineering:true } },
    { path:'/:pathMatch(.*)*', redirect:'/' }
  ],
  scrollBehavior(to, from, savedPosition) {
    if (savedPosition) return savedPosition;
    if (to.fullPath !== from.fullPath) return { top:0 };
    return false;
  }
});
