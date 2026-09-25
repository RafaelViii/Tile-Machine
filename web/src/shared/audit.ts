// Activity log (/audit). Entries are written by the web app in the SAME multi-path update as the change
// they describe, so a change can't be saved without its entry. The rules pin uid/email/ts to the signed-in
// user and server time, operators can't read, edit or delete entries (docs/DATA_MODEL.md "Activity log").
import { push, ref, serverTimestamp, update } from 'firebase/database';
import { auth, db } from '../lib/firebase';
import type { AuditAction, AuditChange } from './types/rtdb';

export interface AuditFields {
  summary: string;
  module?: string;
  changes?: AuditChange[];
  cmdId?: string;
}

/** A new /audit entry as [path, value], to add to a multi-path update. */
export function auditEntry(action: AuditAction, f: AuditFields): [string, Record<string, unknown>] {
  const u = auth.currentUser;
  if (!u) throw new Error('Not signed in');
  const key = push(ref(db, 'audit')).key!;
  const value: Record<string, unknown> = {
    ts: serverTimestamp(),
    uid: u.uid,
    email: u.email ?? '',
    action,
    summary: f.summary,
  };
  if (f.module) value.module = f.module;
  if (f.changes?.length) value.changes = f.changes.slice(0, 60); // a full Containing preset change fits
  if (f.cmdId) value.cmdId = f.cmdId;
  return [`audit/${key}`, value];
}

/** Write one entry on its own (sign-in / sign-out, where there's no other data to change). */
export function logAudit(action: AuditAction, f: AuditFields): Promise<void> {
  const [path, value] = auditEntry(action, f);
  return update(ref(db), { [path]: value });
}
