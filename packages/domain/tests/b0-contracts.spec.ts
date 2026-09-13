import { describe, expect, it } from 'vitest';
import { applyCommandEvent, observeCommand, validateAlarm, validateCommand, validateDashboardSnapshot, type Command } from '../src';

describe('B0 trustworthy contracts', () => {
  it('keeps APPLIED authoritative when local observation is stale and accepts later CONFIRMED', () => {
    const applied: Command = { id:'cmd-1', deviceId:'d1', action:'device.selfTest', state:'APPLIED', issuedAt:1000, updatedAt:2000, expiresAt:2500, feedbackStatus:'PENDING' };
    const observation = observeCommand(applied, 5000, 1000);
    expect(observation.status).toBe('STALE');
    expect(applied.state).toBe('APPLIED');
    const result = applyCommandEvent(applied, { commandId:'cmd-1', state:'CONFIRMED', timestamp:5100, seq:5, feedbackStatus:'CONFIRMED', feedbackAt:5100 });
    expect(result.accepted).toBe(true);
    if (result.accepted) expect(result.command.state).toBe('CONFIRMED');
  });

  it('accepts backend feedback UNKNOWN and NOT_APPLIED', () => {
    expect(validateCommand({ id:'u', deviceId:'d', action:'a', state:'OUTCOME_UNKNOWN', issuedAt:1, updatedAt:2, feedbackStatus:'UNKNOWN' }).feedbackStatus).toBe('UNKNOWN');
    expect(validateCommand({ id:'n', deviceId:'d', action:'a', state:'REJECTED', issuedAt:1, updatedAt:2, feedbackStatus:'NOT_APPLIED' }).feedbackStatus).toBe('NOT_APPLIED');
  });

  it('preserves alarm occurrence/source and marks legacy source as UNKNOWN', () => {
    const current = validateAlarm({ id:'a1', occurrenceId:'occ-1', sourceDomain:'FIELD', deviceId:'d', title:'x', message:'x', severity:'WARNING', state:'ACTIVE', raisedAt:1 });
    expect(current.occurrenceId).toBe('occ-1');
    const legacy = validateAlarm({ id:'a2', deviceId:'d', title:'x', message:'x', severity:'INFO', state:'ACTIVE', raisedAt:1 });
    expect(legacy.sourceDomain).toBe('UNKNOWN');
  });

  it('does not infer real provenance when a legacy payload omits it', () => {
    const snapshot = validateDashboardSnapshot({ siteName:'s', generatedAt:1, provenance:undefined as never, devices:[], telemetry:[], alarms:[], commands:[], communication:[], scenarios:[
      {type:'temperature_humidity',label:'a',description:'',deviceCount:0,onlineCount:0,activeAlarmCount:0},
      {type:'pir_lighting',label:'b',description:'',deviceCount:0,onlineCount:0,activeAlarmCount:0},
      {type:'hazardous_gas',label:'c',description:'',deviceCount:0,onlineCount:0,activeAlarmCount:0},
      {type:'agv_obstacle',label:'d',description:'',deviceCount:0,onlineCount:0,activeAlarmCount:0},
      {type:'goods_counting',label:'e',description:'',deviceCount:0,onlineCount:0,activeAlarmCount:0}
    ] });
    expect(snapshot.provenance.originKind).toBe('UNKNOWN');
  });
});
