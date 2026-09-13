import type { Command } from './models';

export type CommandObservationStatus = 'CURRENT' | 'WAITING' | 'STALE';
export interface CommandObservation { status: CommandObservationStatus; observedAt:number; ageMs:number; }

export function observeCommand(command: Command, now: number, timeoutMs: number): CommandObservation {
  const ageMs = Math.max(0, now - command.updatedAt);
  const terminal = ['REJECTED','FAILED','CONFIRMED','EXPIRED','OUTCOME_UNKNOWN'].includes(command.state);
  if (terminal) return { status:'CURRENT', observedAt:now, ageMs };
  if (ageMs >= timeoutMs) return { status:'STALE', observedAt:now, ageMs };
  return { status:'WAITING', observedAt:now, ageMs };
}
