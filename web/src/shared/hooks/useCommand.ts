import { useCallback, useEffect, useRef, useState } from 'react';
import { increment, onValue, push, ref, serverTimestamp, set, update } from 'firebase/database';
import { auth, db } from '../../lib/firebase';
import { auditEntry } from '../audit';
import type { CommandStatus, CommandType, ModuleId } from '../types/rtdb';

const COMMAND_NO_ANSWER_MS = 10_000;

const MODULE_LABEL: Record<ModuleId, string> = { shredder: 'Shredder', containing: 'Containing', hotpress: 'Hot Press' };

export interface CommandState {
  status: CommandStatus | 'sending' | 'error' | 'noanswer' | null;
  error: string | null;
}

/**
 * Sends a command to /commands/{moduleId|all} and follows its status as the hub
 * updates it (pending → sent → done/failed/expired).
 */
export function useCommand(moduleId: ModuleId | 'all') {
  const [state, setState] = useState<CommandState>({ status: null, error: null });
  const unsub = useRef<(() => void) | null>(null);
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(
    () => () => {
      unsub.current?.();
      if (timer.current) clearTimeout(timer.current);
    },
    [],
  );

  const send = useCallback(
    async (type: CommandType, opts: { target?: number; arg?: number } = {}) => {
      unsub.current?.();
      if (timer.current) clearTimeout(timer.current);
      setState({ status: 'sending', error: null });
      try {
        const body: Record<string, unknown> = {
          type,
          createdAt: serverTimestamp(),
          by: auth.currentUser?.uid ?? 'unknown',
          status: 'pending',
        };
        if (opts.target !== undefined) body.target = opts.target;
        if (opts.arg !== undefined) body.arg = opts.arg;
        // Command + its Activity-log entry + the hub's wake-up signal in one atomic write (the hub reads
        // /signal every second and only then downloads /commands: docs/DATA_MODEL.md "Signal").
        const cmdKey = push(ref(db, `commands/${moduleId}`)).key!;
        const where = moduleId === 'all' ? 'all modules' : MODULE_LABEL[moduleId];
        const extra = opts.target !== undefined ? ` (unit ${opts.target}${opts.arg !== undefined ? `, ${opts.arg}` : ''})` : '';
        const [auditPath, entry] = auditEntry('COMMAND', {
          module: moduleId,
          summary: `${type} → ${where}${extra}`,
          cmdId: cmdKey,
        });
        await update(ref(db), {
          [`commands/${moduleId}/${cmdKey}`]: body,
          [auditPath]: entry,
          'signal/commands': increment(1),
        });
        let logged = false;
        // The hub normally answers in well under a second. Still 'pending' after this = it didn't pick the
        // command up (hub offline, or a hub bug like fw <= 0.3.3 dropping web commands): say so, don't wait forever.
        timer.current = setTimeout(
          () => setState((cur) => (cur.status === 'pending' ? { status: 'noanswer', error: null } : cur)),
          COMMAND_NO_ANSWER_MS,
        );
        unsub.current = onValue(ref(db, `commands/${moduleId}/${cmdKey}/status`), (snap) => {
          // Hub deletes finished commands after 24 h; keep the last known status.
          if (!snap.exists()) return;
          const st = snap.val() as CommandStatus;
          setState({ status: st, error: null });
          if (!logged && (st === 'done' || st === 'failed' || st === 'expired')) {
            logged = true;
            set(ref(db, `${auditPath}/result`), st).catch(() => {}); // best effort: the page may close first
          }
        });
      } catch (e) {
        setState({ status: 'error', error: e instanceof Error ? e.message : String(e) });
      }
    },
    [moduleId],
  );

  /** Back to idle (the button shows its normal icon again). */
  const reset = useCallback(() => setState({ status: null, error: null }), []);

  return { ...state, send, reset };
}
