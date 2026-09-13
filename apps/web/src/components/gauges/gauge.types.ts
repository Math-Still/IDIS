export type GaugeTone = 'info' | 'normal' | 'warning' | 'alarm' | 'offline';

export interface GaugeThresholds {
  warningLow?: number;
  warningHigh?: number;
  alarmLow?: number;
  alarmHigh?: number;
}
