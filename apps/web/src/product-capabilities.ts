export type CapabilityStatus = 'LIVE' | 'PLATFORM' | 'DEMO';

export type CapabilityItem = {
  id:string;
  title:string;
  description:string;
  status:CapabilityStatus;
  link?:string;
  action?:string;
  detail?:string[];
};

export type CapabilityTab = {
  id:string;
  label:string;
  description:string;
  items:CapabilityItem[];
};

export type WorkspaceDefinition = {
  id:string;
  title:string;
  eyebrow:string;
  description:string;
  tabs:CapabilityTab[];
};

export const capabilityStatusLabel:Record<CapabilityStatus,string> = {
  LIVE:'已接入', PLATFORM:'平台能力', DEMO:'演示层'
};

export const workspaceDefinitions:Record<string,WorkspaceDefinition> = {
  safety:{
    id:'safety', title:'安全监测', eyebrow:'安全与现场感知',
    description:'统一承载五类现场安全感知、人员安全、报警与应急联动。实时数据优先使用现有采集链路，未接入能力明确标记为演示层。',
    tabs:[
      {id:'sensing',label:'安全感知',description:'现场五类感知能力统一进入同一工作台。',items:[
        {id:'temperature',title:'温湿度监控',description:'精矿库及现场环境温湿度监测。',status:'LIVE',link:'/applications',action:'进入监测应用'},
        {id:'lighting',title:'红外感应照明',description:'人员感应、照明状态与自动控制模式监测。',status:'LIVE',link:'/applications',action:'进入监测应用'},
        {id:'gas',title:'危险气体监测',description:'SO₂ / CO 等危险气体浓度、质量状态和联动设备。',status:'LIVE',link:'/applications',action:'进入监测应用'},
        {id:'agv',title:'AGV 避障',description:'障碍距离、车辆状态、速度与本地避障状态。',status:'LIVE',link:'/applications',action:'进入监测应用'},
        {id:'count',title:'货物感应计数',description:'成品转运计数、速率和班次目标。',status:'LIVE',link:'/applications',action:'进入监测应用'}
      ]},
      {id:'personnel',label:'人员与巡检',description:'把人员安全相关能力集中管理，避免拆成大量孤立页面。',items:[
        {id:'personnel-safe',title:'人员安全管理',description:'人员状态、风险事件与安全记录的统一入口。',status:'DEMO',detail:['当前版本提供产品展示与数据接口占位','现场人员定位/门禁系统接入后切换为真实数据源']},
        {id:'geofence',title:'电子围栏',description:'危险区域边界、越界事件和区域授权展示。',status:'DEMO',detail:['本地演示区域与事件模型','不伪造真实定位坐标或门禁控制']},
        {id:'inspection',title:'安全巡检',description:'巡检计划、异常发现和处置闭环。',status:'DEMO',detail:['演示巡检任务与状态结构','后续可由应用生态挂载专用巡检应用']}
      ]},
      {id:'response',label:'报警与联动',description:'保留现有报警、事件处置和受控命令链。',items:[
        {id:'alarms',title:'报警中心',description:'活动报警、严重度、确认状态和历史记录。',status:'LIVE',link:'/alarms',action:'查看报警'},
        {id:'incident',title:'应急处置',description:'报警事件转处置、责任人和时间线。',status:'LIVE',link:'/incidents',action:'进入处置'},
        {id:'rules',title:'联动规则',description:'规则配置、告警条件和平台联动策略。',status:'PLATFORM',link:'/rules',action:'管理规则'}
      ]}
    ]
  },
  operations:{
    id:'operations', title:'生产运营', eyebrow:'生产与运营管理',
    description:'把产量、报警、能耗、人员安全和巡检等运营能力收敛到统一工作台，通过统计卡、表格和二级 Tab 组织。',
    tabs:[
      {id:'production',label:'生产统计',description:'面向当班和日常运行的生产数据汇总。',items:[
        {id:'yield',title:'产量统计',description:'利用货物计数链路汇总当前转运量、速率和计划完成情况。',status:'LIVE',link:'/applications',action:'查看生产数据'},
        {id:'alarm-stat',title:'报警统计',description:'按严重度、状态和时间聚合现有报警。',status:'LIVE',link:'/alarms',action:'查看报警统计'},
        {id:'energy-stat',title:'能耗统计',description:'能耗总量、单位产量能耗和负荷结构展示。',status:'DEMO',detail:['当前未接入电表/能源管理系统','演示层与真实采集接口分离']}
      ]},
      {id:'reports',label:'报表与分析',description:'复用现有历史、报表和回放能力。',items:[
        {id:'reports',title:'运行报表',description:'生成和导出运行统计报表。',status:'LIVE',link:'/reports',action:'进入报表'},
        {id:'history',title:'历史回溯',description:'查询历史测点、变化过程和记录。',status:'LIVE',link:'/history',action:'历史回溯'},
        {id:'replay',title:'应用回放',description:'按监测应用回看历史运行过程。',status:'PLATFORM',link:'/applications',action:'选择应用'}
      ]},
      {id:'people',label:'人员与巡检',description:'人员安全和现场巡检能力统一展示。',items:[
        {id:'personnel',title:'人员安全管理',description:'人员状态、安全事件和风险信息。',status:'DEMO'},
        {id:'geofence',title:'电子围栏',description:'区域边界、人员越界和授权状态。',status:'DEMO'},
        {id:'inspection',title:'巡检管理',description:'巡检任务、发现问题、整改与闭环。',status:'DEMO'}
      ]}
    ]
  },
  assets:{
    id:'assets', title:'设备与能源', eyebrow:'资产、能源与双碳',
    description:'统一组织设备台账、点检工单、能耗管理和双碳能力；现场未接入的数据维持明确的演示层边界。',
    tabs:[
      {id:'equipment',label:'设备管理',description:'设备资产与维护业务集中管理。',items:[
        {id:'ledger',title:'设备台账',description:'现有设备、位置、通信协议、运行状态和详情。',status:'LIVE',link:'/devices',action:'查看设备台账'},
        {id:'check',title:'点检',description:'点检计划、项目、结果和异常记录。',status:'DEMO'},
        {id:'workorder',title:'工单',description:'维护工单、处理进度和结果闭环。',status:'DEMO'},
        {id:'communication',title:'设备通信',description:'协议、通信质量、延迟和异常链路。',status:'LIVE',link:'/communication',action:'查看通信'}
      ]},
      {id:'energy',label:'能源管理',description:'展示能源结构、负荷和优化入口。',items:[
        {id:'energy-manage',title:'能耗管理',description:'综合能耗、单位产量能耗、能源介质和负荷状态。',status:'DEMO'},
        {id:'optimization',title:'节能建议',description:'结合运行状态形成可解释的节能建议入口。',status:'DEMO'},
        {id:'energy-ai',title:'能耗优化',description:'为后续优化算法和智能体应用提供挂载位。',status:'DEMO',link:'/intelligence',action:'进入智能分析'}
      ]},
      {id:'carbon',label:'双碳管理',description:'碳排核算与减排管理能力。',items:[
        {id:'carbon-manage',title:'双碳管理',description:'组织碳排边界、排放源和指标体系。',status:'DEMO'},
        {id:'carbon-account',title:'碳排核算',description:'按能源和生产活动数据形成核算结果。',status:'DEMO'},
        {id:'carbon-advice',title:'减排建议',description:'结合能源结构与设备运行给出节能降碳建议。',status:'DEMO'}
      ]}
    ]
  },
  intelligence:{
    id:'intelligence', title:'智能分析', eyebrow:'AI 与工业智能',
    description:'将 AI 智能体、工艺数字员工、预测性维护、能耗优化和视觉 AI 集中到统一智能工作台。',
    tabs:[
      {id:'agent',label:'AI 智能体',description:'复用现有生产助手和运行状态工具。',items:[
        {id:'agent-main',title:'AI 智能体',description:'读取设备、报警、通信、处置和操作状态进行运行分析。',status:'LIVE',link:'/assistant',action:'打开生产助手'},
        {id:'digital-worker',title:'工艺数字员工',description:'面向工艺流程的任务编排、知识调用和建议生成入口。',status:'DEMO'},
        {id:'rag-agent',title:'知识检索助手',description:'从知识中心检索规程、案例和工艺知识。',status:'DEMO',link:'/knowledge',action:'进入知识中心'}
      ]},
      {id:'optimization',label:'预测与优化',description:'面向设备和能源的算法应用入口。',items:[
        {id:'predictive',title:'预测性维护',description:'设备健康评估、异常趋势和维护建议。',status:'DEMO'},
        {id:'energy-opt',title:'能耗优化',description:'能源负荷分析和优化策略入口。',status:'DEMO'},
        {id:'process-opt',title:'工艺优化',description:'工艺指标、约束和优化任务的应用挂载位。',status:'DEMO'}
      ]},
      {id:'vision',label:'视觉 AI',description:'工业视觉能力集中展示。',items:[
        {id:'vision-ai',title:'视觉 AI',description:'相机、视觉任务和识别结果统一入口。',status:'DEMO'},
        {id:'flame',title:'火焰识别',description:'火焰状态、异常燃烧和告警结果展示。',status:'DEMO'},
        {id:'helmet',title:'安全帽检测',description:'安全帽佩戴识别、违规事件和记录。',status:'DEMO'}
      ]}
    ]
  },
  control:{
    id:'control', title:'可视化与控制', eyebrow:'大屏、远程控制与联动',
    description:'展示型大屏与业务控制分层：大屏负责厂区、状态和仪表，业务页负责受控操作、规则和处置。',
    tabs:[
      {id:'screen',label:'可视化',description:'面向展厅、汇报和指挥场景的数字工厂展示。',items:[
        {id:'screen-main',title:'数字可视化大屏',description:'1920×1080 铜冶炼厂数字孪生主屏。',status:'LIVE',link:'/screen',action:'打开运行大屏'},
        {id:'plant-overview',title:'现场总览',description:'按区域查看现场设备、监测点和状态。',status:'LIVE',link:'/overview',action:'查看现场总览'}
      ]},
      {id:'remote',label:'远程控制',description:'继续使用原命令权限、原因、预览、确认和反馈链。',items:[
        {id:'web-control',title:'Web 远程控制',description:'受控设备命令通过现有命令安全链执行。',status:'LIVE',link:'/screen',action:'打开控制区'},
        {id:'app-control',title:'App 接入',description:'OpenHarmony Shell 保留移动/国产终端接入边界。',status:'PLATFORM'},
        {id:'command-audit',title:'操作追溯',description:'控制操作、确认结果和审计记录。',status:'LIVE',link:'/audit',action:'查看操作记录'}
      ]},
      {id:'linkage',label:'报警联动',description:'报警、规则与处置构成完整闭环。',items:[
        {id:'alarm-link',title:'报警与应急联动',description:'报警触发、处置和核心安全控制入口。',status:'LIVE',link:'/incidents',action:'进入应急处置'},
        {id:'rule-link',title:'联动策略',description:'规则配置和报警条件管理。',status:'PLATFORM',link:'/rules',action:'管理联动规则'}
      ]}
    ]
  }
};
