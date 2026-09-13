import type {
  Alarm,
  Command,
  CommunicationHealth,
  DashboardSnapshot,
  Device,
  ScenarioSummary,
  TelemetryPoint
} from './models';

const SCENARIOS = new Set([
  'temperature_humidity',
  'pir_lighting',
  'hazardous_gas',
  'agv_obstacle',
  'goods_counting'
]);

function fail(message: string): never {
  throw new Error(`Dashboard contract violation: ${message}`);
}

function finiteNonNegative(value: number, label: string): void {
  if (!Number.isFinite(value) || value < 0) fail(`${label} must be a finite non-negative number`);
}

function unique(values: string[], label: string): void {
  const seen = new Set<string>();
  for (const value of values) {
    if (!value) fail(`${label} contains an empty key`);
    if (seen.has(value)) fail(`${label} contains duplicate key: ${value}`);
    seen.add(value);
  }
}

export function validateDevice(device: Device): Device {
  if (!device.id || !device.name) fail('device id/name is required');
  if (!SCENARIOS.has(device.scenario)) fail(`unknown device scenario: ${device.scenario}`);
  finiteNonNegative(device.lastSeen, `device ${device.id}.lastSeen`);
  if (device.pointDefinitions) {
    unique(device.pointDefinitions.map((p) => p.pointId), `device ${device.id}.pointDefinitions`);
    for (const point of device.pointDefinitions) {
      if (!point.pointId || !point.label || !Number.isFinite(point.samplePeriodMs) || point.samplePeriodMs < 10) fail(`device ${device.id} has invalid point definition`);
      if (point.limits) for (const [key, value] of Object.entries(point.limits)) if (!Number.isFinite(value)) fail(`device ${device.id} point ${point.pointId} limit ${key} must be finite`);
    }
  }
  if (device.commandCapabilities) {
    for (const [actionId, capability] of Object.entries(device.commandCapabilities)) {
      if (!actionId || capability.actionId !== actionId || !capability.displayName) fail(`device ${device.id} has invalid command capability: ${actionId}`);
    }
  }
  if (device.connection) {
    if (!device.connection.interfaceType || !device.connection.protocol) fail(`device ${device.id}.connection interfaceType/protocol is required`);
    if (device.connection.parameters !== undefined && (typeof device.connection.parameters !== 'object' || Array.isArray(device.connection.parameters))) {
      fail(`device ${device.id}.connection.parameters must be an object`);
    }
  }
  return device;
}

export function validateTelemetryPoint(point: TelemetryPoint): TelemetryPoint {
  if (!point.deviceId || !point.pointId || !point.label) fail('telemetry deviceId/pointId/label is required');
  finiteNonNegative(point.sampleTs, `telemetry ${point.deviceId}:${point.pointId}.sampleTs`);
  finiteNonNegative(point.receiveTs, `telemetry ${point.deviceId}:${point.pointId}.receiveTs`);
  if (!Number.isInteger(point.seq) || point.seq < 0) fail(`telemetry ${point.deviceId}:${point.pointId}.seq must be a non-negative integer`);
  return point;
}

export function validateAlarm(alarm: Alarm): Alarm {
  if (!alarm.id || !alarm.deviceId || !alarm.title) fail('alarm id/deviceId/title is required');
  if (alarm.occurrenceId !== undefined && !alarm.occurrenceId.trim()) fail(`alarm ${alarm.id}.occurrenceId must not be empty`);
  const sourceDomains = new Set(['FIELD','PLATFORM_ACQUISITION','PLATFORM_STORAGE','RULE','UNKNOWN']);
  if (alarm.sourceDomain === undefined) alarm.sourceDomain = 'UNKNOWN';
  if (!sourceDomains.has(alarm.sourceDomain)) fail(`alarm ${alarm.id}.sourceDomain is invalid`);
  finiteNonNegative(alarm.raisedAt, `alarm ${alarm.id}.raisedAt`);
  if (alarm.acknowledgedAt !== undefined) {
    finiteNonNegative(alarm.acknowledgedAt, `alarm ${alarm.id}.acknowledgedAt`);
    if (alarm.acknowledgedAt < alarm.raisedAt) fail(`alarm ${alarm.id}.acknowledgedAt precedes raisedAt`);
  }
  if (alarm.clearedAt !== undefined) {
    finiteNonNegative(alarm.clearedAt, `alarm ${alarm.id}.clearedAt`);
    if (alarm.clearedAt < alarm.raisedAt) fail(`alarm ${alarm.id}.clearedAt precedes raisedAt`);
    if (alarm.acknowledgedAt !== undefined && alarm.clearedAt < alarm.acknowledgedAt) {
      fail(`alarm ${alarm.id}.clearedAt precedes acknowledgedAt`);
    }
  }
  if (alarm.state === 'ACKNOWLEDGED' && alarm.acknowledgedAt === undefined) {
    fail(`alarm ${alarm.id} is ACKNOWLEDGED without acknowledgedAt`);
  }
  if (alarm.acknowledgedAt !== undefined && !alarm.acknowledgedBy) {
    fail(`alarm ${alarm.id} has acknowledgedAt without acknowledgedBy`);
  }
  if (alarm.clearedBy && alarm.clearedAt === undefined) {
    fail(`alarm ${alarm.id} has clearedBy without clearedAt`);
  }
  if (alarm.state === 'ACKNOWLEDGED' && alarm.clearedAt !== undefined) {
    fail(`alarm ${alarm.id} is ACKNOWLEDGED but already has clearedAt`);
  }
  if (alarm.state === 'CLEARED' && alarm.clearedAt === undefined) {
    fail(`alarm ${alarm.id} is CLEARED without clearedAt`);
  }
  if (alarm.state === 'ACTIVE' && alarm.clearedAt !== undefined) {
    fail(`alarm ${alarm.id} is ACTIVE but has clearedAt`);
  }
  return alarm;
}

export function validateCommand(command: Command): Command {
  if (!command.id || !command.deviceId || !command.action) fail('command id/deviceId/action is required');
  finiteNonNegative(command.issuedAt, `command ${command.id}.issuedAt`);
  finiteNonNegative(command.updatedAt, `command ${command.id}.updatedAt`);
  if (command.updatedAt < command.issuedAt) fail(`command ${command.id}.updatedAt precedes issuedAt`);
  if (command.expiresAt !== undefined) {
    finiteNonNegative(command.expiresAt, `command ${command.id}.expiresAt`);
    if (command.expiresAt < command.issuedAt) fail(`command ${command.id}.expiresAt precedes issuedAt`);
  }
  if (command.revision !== undefined && (!Number.isInteger(command.revision) || command.revision < 1)) fail(`command ${command.id}.revision must be a positive integer`);
  if (command.lastEventSeq !== undefined && (!Number.isInteger(command.lastEventSeq) || command.lastEventSeq < 0)) {
    fail(`command ${command.id}.lastEventSeq must be a non-negative integer`);
  }
  if (command.feedbackStatus !== undefined) {
    const feedbackStates = new Set(['PENDING','CONFIRMED','FAILED','TIMEOUT','UNKNOWN','NOT_APPLIED']);
    if (!feedbackStates.has(command.feedbackStatus)) fail(`command ${command.id}.feedbackStatus is invalid`);
  }
  return command;
}

export function validateCommunicationHealth(item: CommunicationHealth): CommunicationHealth {
  if (!item.deviceId) fail('communication.deviceId is required');
  for (const [key,value] of [
    ['latencyMsP50',item.latencyMsP50],['latencyMsP95',item.latencyMsP95],['latencyMsP99',item.latencyMsP99],
    ['jitterMs',item.jitterMs],['messageRatePerMin',item.messageRatePerMin],['reconnectCount',item.reconnectCount],
    ['errorCount',item.errorCount],['lastSeen',item.lastSeen]
  ] as const) if (value !== undefined) finiteNonNegative(value, `communication ${item.deviceId}.${key}`);
  if (item.latencyMsP50 !== undefined && item.latencyMsP95 !== undefined && item.latencyMsP99 !== undefined &&
      !(item.latencyMsP50 <= item.latencyMsP95 && item.latencyMsP95 <= item.latencyMsP99)) {
    fail(`communication ${item.deviceId} latency percentiles must satisfy P50 <= P95 <= P99`);
  }
  return item;
}

function validateScenario(item: ScenarioSummary): ScenarioSummary {
  if (!SCENARIOS.has(item.type)) fail(`unknown scenario summary type: ${item.type}`);
  if (!item.label) fail(`scenario ${item.type}.label is required`);
  for (const [key, value] of [
    ['deviceCount', item.deviceCount],
    ['onlineCount', item.onlineCount],
    ['activeAlarmCount', item.activeAlarmCount]
  ] as const) {
    if (!Number.isInteger(value) || value < 0) fail(`scenario ${item.type}.${key} must be a non-negative integer`);
  }
  if (item.onlineCount > item.deviceCount) fail(`scenario ${item.type}.onlineCount exceeds deviceCount`);
  return item;
}

export function validateDashboardSnapshot(snapshot: DashboardSnapshot): DashboardSnapshot {
  if (!snapshot || typeof snapshot !== 'object') fail('snapshot must be an object');
  if (!snapshot.siteName) fail('siteName is required');
  finiteNonNegative(snapshot.generatedAt, 'generatedAt');
  if (!snapshot.provenance) {
    snapshot.provenance = { originKind:'UNKNOWN', sourceId:'unverified', deliveryMode:'LIVE' };
  }
  if (!['REAL_DEVICE','SIMULATION','UNKNOWN'].includes(snapshot.provenance.originKind)) fail('provenance.originKind is invalid');
  if (!['LIVE','REPLAY'].includes(snapshot.provenance.deliveryMode)) fail('provenance.deliveryMode is invalid');
  if (!snapshot.provenance.sourceId?.trim()) snapshot.provenance.sourceId = 'unverified';
  if (!Array.isArray(snapshot.devices) || !Array.isArray(snapshot.telemetry) || !Array.isArray(snapshot.alarms) ||
      !Array.isArray(snapshot.commands) || !Array.isArray(snapshot.communication) || !Array.isArray(snapshot.scenarios)) {
    fail('snapshot arrays are missing');
  }

  snapshot.devices.forEach(validateDevice);
  snapshot.telemetry.forEach(validateTelemetryPoint);
  snapshot.alarms.forEach(validateAlarm);
  snapshot.commands.forEach(validateCommand);
  snapshot.communication.forEach(validateCommunicationHealth);
  snapshot.scenarios.forEach(validateScenario);

  unique(snapshot.devices.map((d) => d.id), 'devices');
  unique(snapshot.telemetry.map((p) => `${p.deviceId}:${p.pointId}`), 'telemetry');
  unique(snapshot.alarms.map((a) => a.id), 'alarms');
  unique(snapshot.commands.map((c) => c.id), 'commands');
  unique(snapshot.communication.map((c) => c.deviceId), 'communication');
  unique(snapshot.scenarios.map((s) => s.type), 'scenarios');
  for (const expected of SCENARIOS) {
    if (!snapshot.scenarios.some((s) => s.type === expected)) fail(`missing required scenario summary: ${expected}`);
  }

  const deviceIds = new Set(snapshot.devices.map((d) => d.id));
  for (const point of snapshot.telemetry) if (!deviceIds.has(point.deviceId)) fail(`telemetry references unknown device: ${point.deviceId}`);
  for (const alarm of snapshot.alarms) {
    const platformAlarm = alarm.deviceId === 'platform' && alarm.id.startsWith('system-');
    if (!deviceIds.has(alarm.deviceId) && !platformAlarm) fail(`alarm ${alarm.id} references unknown device: ${alarm.deviceId}`);
  }
  for (const command of snapshot.commands) if (!deviceIds.has(command.deviceId)) fail(`command ${command.id} references unknown device: ${command.deviceId}`);
  for (const health of snapshot.communication) if (!deviceIds.has(health.deviceId)) fail(`communication references unknown device: ${health.deviceId}`);

  for (const summary of snapshot.scenarios) {
    const scenarioDevices = snapshot.devices.filter((d) => d.scenario === summary.type);
    const ids = new Set(scenarioDevices.map((d) => d.id));
    const expectedDeviceCount = scenarioDevices.length;
    const expectedOnlineCount = scenarioDevices.filter((d) => d.status !== 'OFFLINE').length;
    const expectedAlarmCount = snapshot.alarms.filter((a) => ids.has(a.deviceId) && a.state !== 'CLEARED').length;
    if (summary.deviceCount !== expectedDeviceCount || summary.onlineCount !== expectedOnlineCount || summary.activeAlarmCount !== expectedAlarmCount) {
      fail(`scenario ${summary.type} counters are inconsistent with devices/alarms`);
    }
  }

  return snapshot;
}
