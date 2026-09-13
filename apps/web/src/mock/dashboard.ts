import {
  scenarioDescriptions, scenarioLabels,
  type Alarm, type CommunicationHealth, type DashboardSnapshot, type Device, type ScenarioType, type TelemetryPoint
} from '@smart-factory/domain';

const now=Date.now();
const provenance={originKind:'SIMULATION' as const,sourceId:'builtin-source',deliveryMode:'LIVE' as const,sourceTimeBasis:'HOST' as const};
const conn=(interfaceType:string,protocol:string,id:string,driver:string):Device['connection']=>({interfaceType,protocol,endpoint:`SIMULATED://${id}`,driver});
const commandCapabilities:NonNullable<Device['commandCapabilities']>={
  'device.start':{actionId:'device.start',displayName:'应急通风启动',requiredPermission:'command.issue',executionClass:'NORMAL',reasonRequired:true,parameterSchema:{type:'none',required:false}},
  'device.stop':{actionId:'device.stop',displayName:'应急通风停止',requiredPermission:'command.issue',executionClass:'NORMAL',reasonRequired:true,parameterSchema:{type:'none',required:false}},
  'device.emergencyStop':{actionId:'device.emergencyStop',displayName:'紧急停机',requiredPermission:'command.emergency',executionClass:'EMERGENCY',reasonRequired:true,parameterSchema:{type:'none',required:false}}
};
export const demoDevices:Device[]=[
  {id:'env-01',name:'精矿库温湿度监测节点',scenario:'temperature_humidity',status:'ONLINE',location:'精矿库 A 区',protocol:'Modbus RTU / RS-485',connection:conn('RS-485','Modbus RTU','env-01','simulated.modbus'),osNode:'Edge-01',lastSeen:now-300,tags:['temperature','humidity'],provenance},
  {id:'env-02',name:'精矿库温湿度监测节点 B',scenario:'temperature_humidity',status:'ONLINE',location:'精矿库 B 区',protocol:'Modbus RTU / RS-485',connection:conn('RS-485','Modbus RTU','env-02','simulated.modbus'),osNode:'Edge-01',lastSeen:now-330,tags:['temperature','humidity'],provenance},
  {id:'light-01',name:'通道红外感应照明节点',scenario:'pir_lighting',status:'ONLINE',location:'电解车间通道 2',protocol:'Discrete I/O / DI/DO',connection:conn('DI/DO','Discrete I/O','light-01','simulated.dio'),osNode:'Edge-01',lastSeen:now-220,tags:['pir','lighting'],provenance},
  {id:'light-02',name:'仓储通道红外照明节点',scenario:'pir_lighting',status:'ONLINE',location:'成品仓储通道',protocol:'Discrete I/O / DI/DO',connection:conn('DI/DO','Discrete I/O','light-02','simulated.dio'),osNode:'Edge-03',lastSeen:now-260,tags:['pir','lighting'],provenance},
  {id:'safety-01',name:'转炉区危气监测与通风联动节点',scenario:'hazardous_gas',status:'ONLINE',location:'转炉区域',protocol:'Modbus RTU / RS-485',connection:conn('RS-485','Modbus RTU','safety-01','simulated.modbus'),osNode:'Edge-02',lastSeen:now-180,tags:['safety','gas'],commandCapabilities,provenance},
  {id:'safety-02',name:'精炼区危险气体监测节点',scenario:'hazardous_gas',status:'ONLINE',location:'精炼炉区域',protocol:'Modbus RTU / RS-485',connection:conn('RS-485','Modbus RTU','safety-02','simulated.modbus'),osNode:'Edge-02',lastSeen:now-190,tags:['safety','gas'],provenance},
  {id:'motion-01',name:'AGV 避障感知节点',scenario:'agv_obstacle',status:'DEGRADED',location:'原料转运通道',protocol:'Vendor Serial Frame / UART',connection:conn('UART','Vendor Serial Frame','motion-01','simulated.serial'),osNode:'Edge-02',lastSeen:now-1800,tags:['motion','agv','ultrasonic'],provenance},
  {id:'count-01',name:'货物感应计数节点',scenario:'goods_counting',status:'ONLINE',location:'成品转运线 A',protocol:'High Speed Counter / DI/HSC',connection:conn('DI/HSC','High Speed Counter','count-01','simulated.counter'),osNode:'Edge-03',lastSeen:now-170,tags:['photoelectric','count'],provenance},
  {id:'count-02',name:'成品线货物计数节点 B',scenario:'goods_counting',status:'ONLINE',location:'成品转运线 B',protocol:'High Speed Counter / DI/HSC',connection:conn('DI/HSC','High Speed Counter','count-02','simulated.counter'),osNode:'Edge-03',lastSeen:now-210,tags:['photoelectric','count'],provenance}
];
const tp=(deviceId:string,pointId:string,label:string,value:number|string|boolean,unit:string|undefined,quality:'GOOD'|'UNCERTAIN'='GOOD',seq=1001):TelemetryPoint=>({deviceId,pointId,label,value,unit,quality,sampleTs:now-320,receiveTs:now-280,seq,provenance,sourceEpoch:'builtin-01',configRevision:'cfg-builtin',contextId:'live:southwest-copper'});
export const demoTelemetry:TelemetryPoint[]=[
  tp('env-01','temperature','温度',26.8,'℃'),tp('env-01','humidity','湿度',59.4,'%RH'),tp('env-02','temperature','温度',27.3,'℃'),tp('env-02','humidity','湿度',61.2,'%RH'),
  tp('light-01','occupied','人员感应',true,undefined),tp('light-01','lightState','照明状态','ON',undefined),tp('light-01','lightMode','控制模式','AUTO',undefined),
  tp('light-02','occupied','人员感应',false,undefined),tp('light-02','lightState','照明状态','OFF',undefined),tp('light-02','lightMode','控制模式','AUTO',undefined),
  tp('safety-01','level','气体浓度',12.4,'ppm'),tp('safety-01','so2','烟气 SO₂',186,'mg/m³'),tp('safety-01','o2','烟气 O₂',4.8,'%'),tp('safety-01','co','烟气 CO',28,'mg/m³'),tp('safety-01','pressure','炉区压力',1.2,'kPa'),tp('safety-01','fanRunning','应急通风',false,undefined),tp('safety-02','level','气体浓度',28.7,'ppm'),tp('safety-02','fanRunning','应急通风',true,undefined),
  tp('motion-01','distance','前向障碍距离',78.5,'cm','UNCERTAIN'),tp('motion-01','motionState','车辆状态','RUNNING',undefined),tp('motion-01','speed','运行速度',1.08,'m/s'),tp('motion-01','position','当前位置','原料通道 A-12',undefined),tp('motion-01','task','当前任务','原料转运',undefined),
  tp('count-01','totalCount','累计计数',18432,'件'),tp('count-01','rate','当前速率',42,'件/min'),tp('count-01','target','班次目标',22000,'件'),tp('count-02','totalCount','累计计数',15380,'件'),tp('count-02','rate','当前速率',36,'件/min'),tp('count-02','target','班次目标',19000,'件')
];
export const demoAlarms:Alarm[]=[
  {id:'alarm-gas-02',occurrenceId:'occ-gas-02',sourceDomain:'FIELD',deviceId:'safety-02',title:'精炼区气体浓度偏高',message:'当前浓度处于关注区间，请检查通风状态。',severity:'WARNING',state:'ACTIVE',raisedAt:now-95_000},
  {id:'alarm-agv-01',occurrenceId:'occ-agv-01',sourceDomain:'PLATFORM_ACQUISITION',deviceId:'motion-01',title:'AGV 通信质量下降',message:'通信延迟和抖动上升，当前车辆保持本地避障控制。',severity:'WARNING',state:'ACTIVE',raisedAt:now-160_000}
];
const comm=(deviceId:string,quality:'GOOD'|'UNCERTAIN',p50:number,p95:number,p99:number,jitter:number,rate:number,reconnect=0,error=0):CommunicationHealth=>({deviceId,online:true,quality,latencyMsP50:p50,latencyMsP95:p95,latencyMsP99:p99,jitterMs:jitter,messageRatePerMin:rate,reconnectCount:reconnect,errorCount:error,lastSeen:now-250,provenance});
export const demoCommunication:CommunicationHealth[]=[comm('env-01','GOOD',7,12,17,3,120),comm('env-02','GOOD',8,13,18,3,120),comm('light-01','GOOD',4,8,12,2,60),comm('light-02','GOOD',5,9,13,2,60),comm('safety-01','GOOD',7,12,16,3,120),comm('safety-02','GOOD',7,13,18,3,120),comm('motion-01','UNCERTAIN',28,86,124,24,180,2,3),comm('count-01','GOOD',5,9,13,2,90),comm('count-02','GOOD',6,10,15,2,90)];
const scenarioTypes:ScenarioType[]=['temperature_humidity','pir_lighting','hazardous_gas','agv_obstacle','goods_counting'];
export const demoDashboard:DashboardSnapshot={siteName:'西南铜业智慧工厂',generatedAt:now,provenance,devices:demoDevices,telemetry:demoTelemetry,alarms:demoAlarms,commands:[],communication:demoCommunication,scenarios:scenarioTypes.map(type=>({type,label:scenarioLabels[type],description:scenarioDescriptions[type],deviceCount:demoDevices.filter(d=>d.scenario===type).length,onlineCount:demoDevices.filter(d=>d.scenario===type&&d.status!=='OFFLINE').length,activeAlarmCount:demoAlarms.filter(a=>a.state!=='CLEARED'&&demoDevices.find(d=>d.id===a.deviceId)?.scenario===type).length}))};
