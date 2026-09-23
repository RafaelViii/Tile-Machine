import { useCallback, useEffect, useMemo, useState } from 'react';
import { ref, runTransaction } from 'firebase/database';
import { db } from '../../lib/firebase';
import { normalizeConfig } from '../configDefaults';
import type { ConfigApplied, ConfigByModule, ModuleId } from '../types/rtdb';

export type SyncState = 'never' | 'synced' | 'pending' | 'queued' | 'rejected';

/**
 * Editable draft of a module's config. Saving bumps `version` in a transaction
 * (the RTDB rules require a strictly increasing version).
 */
export function useConfigEditor<M extends ModuleId>(
  id: M,
  stored: ConfigByModule[M] | undefined,
  applied: ConfigApplied | undefined,
) {
  const saved = useMemo(() => normalizeConfig(id, stored), [id, stored]);
  const [draft, setDraft] = useState<ConfigByModule[M]>(saved);
  const [dirty, setDirty] = useState(false);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  // Follow the stored config while the user hasn't touched the form.
  useEffect(() => {
    if (!dirty) setDraft(saved);
  }, [saved, dirty]);

  const update = useCallback((fn: (d: ConfigByModule[M]) => ConfigByModule[M]) => {
    setDraft((d) => fn(structuredClone(d)));
    setDirty(true);
  }, []);

  const reset = useCallback(() => {
    setDraft(saved);
    setDirty(false);
    setError(null);
  }, [saved]);

  const save = useCallback(async () => {
    setSaving(true);
    setError(null);
    try {
      await runTransaction(ref(db, `modules/${id}/config`), (current) => {
        const prev = (current as { version?: number } | null)?.version ?? 0;
        return { ...draft, version: prev + 1 };
      });
      setDirty(false);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setSaving(false);
    }
  }, [id, draft]);

  const sync: SyncState = !stored
    ? 'never'
    : applied?.version === saved.version
      ? applied.result === 'rejected'
        ? 'rejected'
        : applied.result === 'queued'
          ? 'queued'
          : 'synced'
      : 'pending';

  return { draft, update, dirty, saving, error, save, reset, sync, version: saved.version };
}
