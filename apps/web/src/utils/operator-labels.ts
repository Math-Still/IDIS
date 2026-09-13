import type {
  AlarmLifecycleEventType,
  AlarmState,
  AuditEvent,
  Command,
  CommandFeedbackStatus,
  CommandState,
  UserRole
} from '@smart-factory/domain';
import { safeDisplayText } from './industrial-format';

export const roleLabel: Record<UserRole, string> = {
  OBSERVER: '观察员',
  OPERATOR: '操作员',
  ENGINEER: '工程师',
  ADMINISTRATOR: '管理员'
};

export const alarmStateLabel: Record<AlarmState, string> = {
  ACTIVE: '未确认',
  ACKNOWLEDGED: '已确认',
  CLEARED: '已恢复'
};

export const alarmLifecycleLabel: Record<AlarmLifecycleEventType, string> = {
  RAISED: '报警产生',
  ACKNOWLEDGED: '操作员确认',
  CLEARED: '现场恢复'
};

export const commandStateLabel: Record<CommandState, string> = {
  ISSUED: '已下发',
  RECEIVED: '已接收',
  ACCEPTED: '已接受',
  REJECTED: '已拒绝',
  EXECUTING: '执行中',
  APPLIED: '已写入',
  CONFIRMED: '已确认',
  FAILED: '执行失败',
  TIMEOUT: '反馈超时',
  EXPIRED: '已过期',
  OUTCOME_UNKNOWN: '结果未知'
};

export function commandFeedbackLabel(value?: CommandFeedbackStatus): string {
  if (value === 'CONFIRMED') return '现场已确认';
  if (value === 'FAILED') return '现场执行失败';
  if (value === 'TIMEOUT') return '现场反馈超时';
  if (value === 'UNKNOWN') return '现场结果未知';
  if (value === 'NOT_APPLIED') return '未写入现场';
  if (value === 'PENDING') return '等待现场反馈';
  return '—';
}

const COMMAND_ACTION_LABELS: Record<string, string> = {
  'device.selfTest': '设备自检',
  'device.start': '启动设备',
  'device.stop': '停止设备',
  'device.reset': '设备复位',
  'device.emergencyStop': '紧急停止'
};

export function commandActionLabel(action: string): string {
  return COMMAND_ACTION_LABELS[action] ?? '设备控制操作';
}

export function commandOperatorSummary(command: Command): string {
  if (command.state === 'CONFIRMED') return '现场反馈已确认，操作闭环完成。';
  if (command.state === 'FAILED') return '现场设备反馈执行失败，请检查设备状态。';
  if (command.state === 'REJECTED') return '操作请求未被接受，请核对权限、设备状态或操作条件。';
  if (command.state === 'TIMEOUT') return '未在规定时间收到现场确认，请以设备实际状态为准。';
  if (command.state === 'EXPIRED') return '操作请求已超过有效时间，未继续执行。';
  if (command.state === 'OUTCOME_UNKNOWN') return '系统无法确认现场最终结果，请核对设备实际状态后再决定是否重试。';
  if (command.state === 'EXECUTING' || command.state === 'APPLIED') return '操作正在现场执行，等待最终反馈。';
  if (command.state === 'ACCEPTED' || command.state === 'RECEIVED') return '操作已被系统接收，等待现场执行。';
  return '操作已下发，等待系统反馈。';
}

export function lifecycleSourceLabel(source: string): string {
  const normalized = source.trim().toUpperCase();
  if (['FIELD', 'FIELD_RETURN_TO_NORMAL', 'DEVICE', 'ADAPTER'].includes(normalized)) return '现场设备';
  if (['OPERATOR', 'USER', 'HMI'].includes(normalized)) return '操作员';
  if (['SYSTEM', 'BACKEND'].includes(normalized)) return '系统';
  return '系统记录';
}

const AUDIT_ACTION_LABELS: Record<string, string> = {
  AUTH_LOGIN: '用户登录',
  AUTH_REQUEST: '访问认证',
  AUTH_WEBSOCKET: '实时连接认证',
  COMMAND_REQUEST: '设备操作请求',
  COMMAND_RESULT: '设备操作结果',
  ALARM_ACK: '报警确认',
  ALARM_CLEAR_ATTEMPT: '报警恢复处理'
};

const AUDIT_OUTCOME_LABELS: Record<string, string> = {
  SUCCESS: '成功',
  CONFIRMED: '已确认',
  ACCEPTED: '已接受',
  DENIED: '无权限',
  REJECTED: '已拒绝',
  FAILED: '失败',
  CONFLICT: '状态冲突',
  NOT_FOUND: '对象不存在',
  IDEMPOTENT_REPLAY: '重复请求已复用',
  NOOP: '无需处理',
  ISSUED: '已下发',
  RECEIVED: '已接收',
  EXECUTING: '执行中',
  APPLIED: '已写入',
  TIMEOUT: '反馈超时',
  EXPIRED: '已过期',
  OUTCOME_UNKNOWN: '结果未知'
};

const AUDIT_RESOURCE_LABELS: Record<string, string> = {
  AUTH: '用户会话',
  COMMAND: '设备操作',
  ALARM: '报警事件',
  DEVICE: '现场设备'
};

const DETAIL_LABELS: Array<[RegExp, string]> = [
  [/authenticated/i, '身份认证成功。'],
  [/invalid credentials/i, '用户标识或访问令牌无效。'],
  [/missing or invalid bearer token/i, '访问凭据缺失或无效。'],
  [/role is not allowed to issue commands/i, '当前角色没有设备操作权限。'],
  [/highest privilege required for emergency command/i, '当前角色没有紧急操作权限。'],
  [/missing commandId\/deviceId\/action/i, '操作请求缺少必要信息。'],
  [/emergency command requires reason/i, '紧急操作必须填写原因。'],
  [/invalid ttlMs/i, '操作请求的有效时间配置无效。'],
  [/unknown device/i, '未找到对应现场设备。'],
  [/commandId already exists with different payload/i, '同一操作编号已存在不同内容，请重新发起。'],
  [/existing command returned/i, '检测到重复操作请求，已返回原有结果。'],
  [/command accepted for execution/i, '操作请求已进入执行流程。'],
  [/role is not allowed to acknowledge alarms/i, '当前角色没有报警确认权限。'],
  [/alarm not found/i, '未找到对应报警。'],
  [/cleared alarm cannot be acknowledged/i, '已恢复报警无需再次确认。'],
  [/alarm already acknowledged/i, '报警此前已经确认。'],
  [/alarm acknowledged/i, '报警已由操作员确认。'],
  [/manual clear is prohibited/i, '报警只能随现场条件恢复自动结束，不能人工清除。'],
  [/alarm already cleared by field return-to-normal/i, '现场条件已恢复，报警已结束。']
];

export function auditActionLabel(action: string): string {
  return AUDIT_ACTION_LABELS[action] ?? '系统操作';
}

export function auditOutcomeLabel(outcome: string): string {
  return AUDIT_OUTCOME_LABELS[outcome] ?? '已记录';
}

export function auditResourceLabel(resourceType: string): string {
  return AUDIT_RESOURCE_LABELS[resourceType] ?? '系统对象';
}

export function auditDetailLabel(event: Pick<AuditEvent, 'detail' | 'reason'>): string {
  const detail = safeDisplayText(event.detail, '', 160);
  for (const [pattern, label] of DETAIL_LABELS) {
    if (pattern.test(detail)) return label;
  }
  if (!detail) return event.reason ? '操作原因已记录。' : '系统已记录该操作。';
  // Do not expose backend implementation strings on business pages. Chinese
  // operator-entered detail remains readable; opaque English/internal tokens
  // stay available in engineering diagnostics and persisted audit storage.
  if (/[A-Za-z_]{4,}/.test(detail)) return '系统已记录该操作结果。';
  return detail;
}

export function roleDisplay(role: UserRole | string | undefined): string {
  if (!role) return '—';
  return roleLabel[role as UserRole] ?? '系统用户';
}
