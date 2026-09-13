import type { Command, CommandEvent, CommandState } from './models';

// TIMEOUT is deliberately not terminal: it means the client has not observed a
// status update within the local observation window. A later authoritative ACK
// can still recover the actual device/backend outcome.
export const TERMINAL_COMMAND_STATES = new Set<CommandState>([
  'REJECTED', 'FAILED', 'CONFIRMED', 'EXPIRED', 'OUTCOME_UNKNOWN'
]);

const ALLOWED: Record<CommandState, Set<CommandState>> = {
  ISSUED: new Set(['RECEIVED', 'ACCEPTED', 'REJECTED', 'FAILED', 'TIMEOUT', 'EXPIRED', 'OUTCOME_UNKNOWN']),
  RECEIVED: new Set(['ACCEPTED', 'REJECTED', 'FAILED', 'TIMEOUT', 'EXPIRED', 'OUTCOME_UNKNOWN']),
  ACCEPTED: new Set(['EXECUTING', 'APPLIED', 'FAILED', 'TIMEOUT', 'EXPIRED', 'OUTCOME_UNKNOWN']),
  REJECTED: new Set(),
  EXECUTING: new Set(['APPLIED', 'FAILED', 'TIMEOUT', 'EXPIRED', 'OUTCOME_UNKNOWN']),
  APPLIED: new Set(['CONFIRMED', 'FAILED', 'TIMEOUT', 'EXPIRED', 'OUTCOME_UNKNOWN']),
  FAILED: new Set(),
  CONFIRMED: new Set(),
  TIMEOUT: new Set(['RECEIVED', 'ACCEPTED', 'REJECTED', 'EXECUTING', 'APPLIED', 'FAILED', 'CONFIRMED', 'EXPIRED', 'OUTCOME_UNKNOWN']),
  EXPIRED: new Set(),
  OUTCOME_UNKNOWN: new Set()
};

export type CommandEventDecision =
  | { accepted: true; duplicate: boolean; command: Command }
  | {
      accepted: false;
      duplicate: false;
      reason: 'OUT_OF_ORDER_SEQ' | 'OUT_OF_ORDER_REVISION' | 'ILLEGAL_TRANSITION' | 'TERMINAL_STATE' | 'INVALID_EVENT';
      command: Command;
    };

function validEvent(event: CommandEvent): boolean {
  if (!event.commandId || !Number.isFinite(event.timestamp) || event.timestamp < 0) return false;
  if (event.seq !== undefined && (!Number.isInteger(event.seq) || event.seq < 0)) return false;
  if (event.revision !== undefined && (!Number.isInteger(event.revision) || event.revision < 1)) return false;
  return true;
}

export function applyCommandEvent(command: Command, event: CommandEvent): CommandEventDecision {
  if (!validEvent(event) || event.commandId !== command.id) {
    return { accepted: false, duplicate: false, reason: 'INVALID_EVENT', command };
  }

  const hasComparableRevision = event.revision !== undefined && command.revision !== undefined;
  if (hasComparableRevision && event.revision! < command.revision!) {
    return { accepted: false, duplicate: false, reason: 'OUT_OF_ORDER_REVISION', command };
  }
  if (hasComparableRevision && event.revision === command.revision) {
    if (event.state === command.state) return { accepted: true, duplicate: true, command };
    return { accepted: false, duplicate: false, reason: 'OUT_OF_ORDER_REVISION', command };
  }

  const hasComparableSeq = event.seq !== undefined && command.lastEventSeq !== undefined;
  if (hasComparableSeq && event.seq! < command.lastEventSeq!) {
    return { accepted: false, duplicate: false, reason: 'OUT_OF_ORDER_SEQ', command };
  }

  if (hasComparableSeq && event.seq === command.lastEventSeq) {
    if (event.state === command.state) return { accepted: true, duplicate: true, command };
    return { accepted: false, duplicate: false, reason: 'OUT_OF_ORDER_SEQ', command };
  }

  if (event.state === command.state) {
    // A first explicit sequence establishes the ordering baseline even if the
    // state itself did not change. Repeating the same state without a new seq
    // remains idempotent and does not extend the timeout window.
    if (event.seq === undefined) return { accepted: true, duplicate: true, command };
    if (command.lastEventSeq === undefined || event.seq > command.lastEventSeq) {
      return {
        accepted: true,
        duplicate: false,
        command: {
          ...command,
          updatedAt: Math.max(command.updatedAt, event.timestamp),
          lastEventSeq: event.seq,
          revision: event.revision ?? command.revision,
          detail: event.detail ?? command.detail,
          feedbackStatus: event.feedbackStatus ?? command.feedbackStatus,
          feedbackAt: event.feedbackAt ?? command.feedbackAt,
          deliveryCertainty: event.deliveryCertainty ?? command.deliveryCertainty,
          executionSupervision: event.executionSupervision ?? command.executionSupervision,
          adapterQuarantined: event.adapterQuarantined ?? command.adapterQuarantined
        }
      };
    }
  }

  if (TERMINAL_COMMAND_STATES.has(command.state)) {
    return { accepted: false, duplicate: false, reason: 'TERMINAL_STATE', command };
  }

  if (!ALLOWED[command.state].has(event.state)) {
    return { accepted: false, duplicate: false, reason: 'ILLEGAL_TRANSITION', command };
  }

  return {
    accepted: true,
    duplicate: false,
    command: {
      ...command,
      state: event.state,
      // Never let a clock-regressed remote timestamp move our local command
      // observation time backwards.
      updatedAt: Math.max(command.updatedAt, event.timestamp),
      lastEventSeq: event.seq ?? command.lastEventSeq,
      revision: event.revision ?? command.revision,
      detail: event.detail ?? command.detail,
      feedbackStatus: event.feedbackStatus ?? command.feedbackStatus,
      feedbackAt: event.feedbackAt ?? command.feedbackAt,
      deliveryCertainty: event.deliveryCertainty ?? command.deliveryCertainty,
      executionSupervision: event.executionSupervision ?? command.executionSupervision,
      adapterQuarantined: event.adapterQuarantined ?? command.adapterQuarantined
    }
  };
}

export function expireCommand(command: Command, now: number): Command {
  if (TERMINAL_COMMAND_STATES.has(command.state)) return command;
  if (command.expiresAt !== undefined && now >= command.expiresAt) {
    return { ...command, state: 'EXPIRED', updatedAt: Math.max(command.updatedAt, now), detail: 'Command TTL expired before confirmation.' };
  }
  return command;
}

export function timeoutCommand(command: Command, now: number, timeoutMs: number): Command {
  if (TERMINAL_COMMAND_STATES.has(command.state) || command.state === 'TIMEOUT') return command;
  if (now - command.updatedAt >= timeoutMs) {
    return {
      ...command,
      state: 'TIMEOUT',
      updatedAt: Math.max(command.updatedAt, now),
      detail: 'No command status update before the local observation timeout; backend/device outcome is unknown.'
    };
  }
  return command;
}
