import { useCallback, useEffect, useRef, useState } from 'react';
import { onValue, push, ref, serverTimestamp } from 'firebase/database';
import { auth, db } from '../../lib/firebase';
import type { CommandStatus, CommandType, ModuleId } from '../types/rtdb';

export interface CommandState {
  status: CommandStatus | 'sending' | 'error' | null;
  error: string | null;
}

/**
 * Sends a command to /commands/{moduleId|all} and follows its status as the hub
 * updates it (pending → sent → done/failed/expired).
 */
export function useCommand(moduleId: ModuleId | 'all') {
  const [state, setState] = useState<CommandState>({ status: null, error: null });
  const unsub = useRef<(() => void) | null>(null);

  useEffect(() => () => unsub.current?.(), []);

  const send = useCallback(
    async (type: CommandType, opts: { target?: number; arg?: number } = {}) => {
      unsub.current?.();
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
        const cmdRef = await push(ref(db, `commands/${moduleId}`), body);
        unsub.current = onValue(ref(db, `commands/${moduleId}/${cmdRef.key}/status`), (snap) => {
          // Hub deletes finished commands after 24 h; keep the last known status.
          if (snap.exists()) setState({ status: snap.val() as CommandStatus, error: null });
        });
      } catch (e) {
        setState({ status: 'error', error: e instanceof Error ? e.message : String(e) });
      }
    },
    [moduleId],
  );

  return { ...state, send };
}
