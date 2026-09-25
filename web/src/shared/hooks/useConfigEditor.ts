import { useCallback } from 'react';
import { useConfigDrafts } from '../configDrafts';
import type { ConfigApplied, ConfigByModule, ModuleId } from '../types/rtdb';

export type SyncState = 'never' | 'synced' | 'pending' | 'queued' | 'rejected';

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

  const sync: SyncState = !stored
    ? 'never'
    : applied?.version === saved.version
      ? applied.result === 'rejected'
        ? 'rejected'
        : applied.result === 'queued'
          ? 'queued'
          : 'synced'
      : 'pending';

  return { draft: d.current[id] as ConfigByModule[M], update, dirty: d.dirty[id], sync, version: saved.version };
}
