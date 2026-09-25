import { useCallback, useMemo } from 'react';
import { push, ref, remove, serverTimestamp, update } from 'firebase/database';
import { auth, db } from '../../lib/firebase';
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

  const create = useCallback(async (name: string, configs: Configs) => {
    const r = await push(ref(db, 'presets'), {
      name,
      createdAt: serverTimestamp(),
      updatedAt: serverTimestamp(),
      by: auth.currentUser?.uid ?? 'unknown',
      ...snapshot(configs),
    });
    return r.key!;
  }, []);

  const overwrite = useCallback(
    (id: string, configs: Configs) =>
      update(ref(db, `presets/${id}`), { ...snapshot(configs), updatedAt: serverTimestamp() }),
    [],
  );

  const rename = useCallback(
    (id: string, name: string) => update(ref(db, `presets/${id}`), { name, updatedAt: serverTimestamp() }),
    [],
  );

  const del = useCallback((id: string) => remove(ref(db, `presets/${id}`)), []);

  return { presets, loading: data === undefined, error, create, overwrite, rename, remove: del };
}
