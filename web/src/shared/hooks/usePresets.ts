import { useCallback, useMemo } from 'react';
import { push, ref, serverTimestamp, update } from 'firebase/database';
import { auth, db } from '../../lib/firebase';
import { auditEntry } from '../audit';
import { MODULE_IDS, type ConfigByModule, type ModuleId, type PresetNode } from '../types/rtdb';
import { useValue } from './useValue';

export const PRESET_NAME_MAX = 40;

export interface Preset extends PresetNode {
  id: string;
}

type Configs = { [M in ModuleId]: ConfigByModule[M] };

function snapshot(configs: Configs) {
  const out: Record<string, unknown> = {};
  for (const id of MODULE_IDS) {
    const { version: _v, ...rest } = configs[id];
    out[id] = rest;
  }
  return out;
}

/** Whole-machine presets at /presets, shared by every admin. Sorted by name. */
export function usePresets() {
  const { data, error } = useValue<Record<string, PresetNode>>('presets');
  const presets = useMemo<Preset[]>(
    () =>
      Object.entries(data ?? {})
        .map(([id, p]) => ({ ...p, id }))
        .sort((a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: 'base' })),
    [data],
  );

  // Every change goes out together with its Activity-log entry (one atomic multi-path update).
  const nameOf = useCallback((id: string) => presets.find((p) => p.id === id)?.name ?? id, [presets]);
  const uid = () => auth.currentUser?.uid ?? 'unknown';

  const create = useCallback(async (name: string, configs: Configs) => {
    const key = push(ref(db, 'presets')).key!;
    const [ap, entry] = auditEntry('PRESET_CREATE', { summary: `Created preset "${name}"` });
    await update(ref(db), {
      [`presets/${key}`]: {
        name,
        createdAt: serverTimestamp(),
        updatedAt: serverTimestamp(),
        by: uid(),
        ...snapshot(configs),
      },
      [ap]: entry,
    });
    return key;
  }, []);

  const overwrite = useCallback(
    (id: string, configs: Configs) => {
      const [ap, entry] = auditEntry('PRESET_UPDATE', { summary: `Updated preset "${nameOf(id)}" with the current settings` });
      const upd: Record<string, unknown> = { [ap]: entry };
      for (const [k, v] of Object.entries({ ...snapshot(configs), updatedAt: serverTimestamp(), updatedBy: uid() }))
        upd[`presets/${id}/${k}`] = v;
      return update(ref(db), upd);
    },
    [nameOf],
  );

  const rename = useCallback(
    (id: string, name: string) => {
      const [ap, entry] = auditEntry('PRESET_RENAME', { summary: `Renamed preset "${nameOf(id)}" to "${name}"` });
      return update(ref(db), {
        [`presets/${id}/name`]: name,
        [`presets/${id}/updatedAt`]: serverTimestamp(),
        [`presets/${id}/updatedBy`]: uid(),
        [ap]: entry,
      });
    },
    [nameOf],
  );

  const del = useCallback(
    (id: string) => {
      const [ap, entry] = auditEntry('PRESET_DELETE', { summary: `Deleted preset "${nameOf(id)}"` });
      return update(ref(db), { [`presets/${id}`]: null, [ap]: entry });
    },
    [nameOf],
  );

  return { presets, loading: data === undefined, error, create, overwrite, rename, remove: del };
}
