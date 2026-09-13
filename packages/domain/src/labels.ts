import type { AlarmSeverity, DataQuality, DeviceStatus, ScenarioType } from './models';

export const scenarioLabels: Record<ScenarioType, string> = {
  temperature_humidity: '温湿度监控',
  pir_lighting: '红外感应照明',
  hazardous_gas: '危险气体监测',
  agv_obstacle: 'AGV 障碍监测',
  goods_counting: '货物计数'
};

export const scenarioDescriptions: Record<ScenarioType, string> = {
  temperature_humidity: '查看区域温度、湿度、通风状态和异常变化。',
  pir_lighting: '查看人员感应、照明状态、运行模式和状态变化。',
  hazardous_gas: '查看各类气体浓度、报警状态和通风联动情况。',
  agv_obstacle: '查看障碍距离、车辆状态、通信情况和停车事件。',
  goods_counting: '查看累计数量、当前速率、班次增量和计数状态。'
};

export const qualityLabel: Record<DataQuality, string> = {
  GOOD: '正常', UNCERTAIN: '待确认', BAD: '异常', STALE: '数据过期'
};
export const deviceStatusLabel: Record<DeviceStatus, string> = {
  ONLINE: '在线', OFFLINE: '离线', DEGRADED: '需关注', MAINTENANCE: '维护'
};
export const severityLabel: Record<AlarmSeverity, string> = {
  INFO: '提示', WARNING: '警告', CRITICAL: '严重'
};
