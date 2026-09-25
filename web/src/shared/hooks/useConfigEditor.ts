import { useCallback } from 'react';
import { useConfigDrafts } from '../configDrafts';
import type { ConfigApplied, ConfigByModule, ModuleId } from '../types/rtdb';

export type SyncState = 'never' | 'synced' | 'pending' | 'queued' | 'rejected';

/** Whether the module has applied the stored config (compares versions). */
export function syncState(stored: { version: number } | null | undefined, applied: ConfigApplied | undefined): SyncState {
  if (!stored) return 'never';
  if (applied?.version !== stored.version) return 'pending';
  return applied.result === 'rejected' ? 'rejected' : applied.result === 'queued' ? 'queued' : 'synced';
}

/**
 * Editable draft of one module's config. Drafts live in <ConfigDraftsProvider> so they survive
 * page changes and a preset can fill every module at once.
 */
export function useConfigEditor<M extends ModuleId>(
  id: M,
  stored: ConfigByModule[M] | undefined,
  applied: ConfigApplied | undefined,
) {
  const d = useConfigDrafts();
  const saved = d.saved[id];
  const setDraft = d.update;
  const update = useCallback(
    (fn: (c: ConfigByModule[M]) => ConfigByModule[M]) => setDraft(id, fn),
    [id, setDraft],
  );

  const sync = syncState(stored, applied);
  return { draft: d.current[id] as ConfigByModule[M], update, dirty: d.dirty[id], sync, version: saved.version };
}
