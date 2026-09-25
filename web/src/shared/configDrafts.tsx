import { createContext, useCallback, useContext, useMemo, useState, type ReactNode } from 'react';
import { ref, runTransaction } from 'firebase/database';
import { db } from '../lib/firebase';
import { normalizeConfig } from './configDefaults';
import { useMachine } from './machine';
import { MODULE_IDS, type ConfigByModule, type ModuleId } from './types/rtdb';

type Configs = { [M in ModuleId]: ConfigByModule[M] };
type Drafts = { [M in ModuleId]?: ConfigByModule[M] };

/** Compare two configs by value, ignoring `version` and key order. */
export function sameConfig(a: unknown, b: unknown): boolean {
  return stableJson(a, true) === stableJson(b, true);
}

function stableJson(v: unknown, top = false): string {
  if (Array.isArray(v)) return `[${v.map((x) => stableJson(x)).join(',')}]`;
  if (v && typeof v === 'object') {
    const keys = Object.keys(v)
      .filter((k) => !(top && k === 'version'))
      .sort();
    return `{${keys.map((k) => `${JSON.stringify(k)}:${stableJson((v as Record<string, unknown>)[k])}`).join(',')}}`;
  }
  return JSON.stringify(v);
}

interface DraftsCtx {
  saved: Configs;
  current: Configs;
  dirty: Record<ModuleId, boolean>;
  dirtyIds: ModuleId[];
  saving: boolean;
  error: string | null;
  update: <M extends ModuleId>(id: M, fn: (d: ConfigByModule[M]) => ConfigByModule[M]) => void;
  /** Replace the drafts of the given modules (e.g. from a preset). Nothing is written until save. */
  load: (configs: Partial<{ [M in ModuleId]: unknown }>) => void;
  reset: () => void;
  saveAll: () => Promise<void>;
  /** Preset last picked or saved in this browser session (null = none). */
  presetId: string | null;
  setPresetId: (id: string | null) => void;
}

const Ctx = createContext<DraftsCtx | null>(null);

/**
 * Unsaved config edits for all modules, kept while switching pages, so a whole-machine preset
 * can fill every form at once. Saving bumps each module's `version` in a transaction
 * (the RTDB rules require a strictly increasing version).
 */
export function ConfigDraftsProvider({ children }: { children: ReactNode }) {
  const { modules } = useMachine();
  const sShred = modules.shredder.node?.config;
  const sCont = modules.containing.node?.config;
  const sHot = modules.hotpress.node?.config;
  const saved = useMemo<Configs>(
    () => ({
      shredder: normalizeConfig('shredder', sShred),
      containing: normalizeConfig('containing', sCont),
      hotpress: normalizeConfig('hotpress', sHot),
    }),
    [sShred, sCont, sHot],
  );

  const [drafts, setDrafts] = useState<Drafts>({});
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [presetId, setPresetId] = useState<string | null>(null);

  // A draft equal to the stored config is not a change, so the form keeps following the stored value.
  const dirty = {} as Record<ModuleId, boolean>;
  const current = {} as Record<ModuleId, unknown>;
  for (const id of MODULE_IDS) {
    const d = drafts[id];
    dirty[id] = d !== undefined && !sameConfig(d, saved[id]);
    current[id] = dirty[id] ? { ...d, version: saved[id].version } : saved[id];
  }
  const dirtyIds = MODULE_IDS.filter((id) => dirty[id]);

  const update = useCallback(
    <M extends ModuleId>(id: M, fn: (d: ConfigByModule[M]) => ConfigByModule[M]) => {
      setDrafts((all) => {
        const d = all[id];
        const base = d !== undefined && !sameConfig(d, saved[id]) ? d : saved[id];
        return { ...all, [id]: fn(structuredClone(base as ConfigByModule[M])) };
      });
    },
    [saved],
  );

  const load = useCallback((configs: Partial<{ [M in ModuleId]: unknown }>) => {
    setDrafts((all) => {
      const next = { ...all } as Record<string, unknown>;
      for (const id of MODULE_IDS) if (configs[id]) next[id] = normalizeConfig(id, configs[id]);
      return next as Drafts;
    });
    setError(null);
  }, []);

  // Back to what the machine uses; the picker then shows a preset only if one matches it exactly.
  const reset = useCallback(() => {
    setDrafts({});
    setError(null);
    setPresetId(null);
  }, []);

  const saveAll = useCallback(async () => {
    setSaving(true);
    setError(null);
    const done: ModuleId[] = [];
    try {
      for (const id of dirtyIds) {
        const draft = drafts[id]!;
        await runTransaction(ref(db, `modules/${id}/config`), (cur) => {
          const prev = (cur as { version?: number } | null)?.version ?? 0;
          return { ...draft, version: prev + 1 };
        });
        done.push(id);
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      // Drop only what reached the database, so a failed module keeps its edits.
      setDrafts((all) => {
        const next = { ...all };
        for (const id of done) delete next[id];
        return next;
      });
      setSaving(false);
    }
  }, [dirtyIds, drafts]);

  const value: DraftsCtx = {
    saved,
    current: current as Configs,
    dirty,
    dirtyIds,
    saving,
    error,
    update,
    load,
    reset,
    saveAll,
    presetId,
    setPresetId,
  };
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}

export function useConfigDrafts(): DraftsCtx {
  const c = useContext(Ctx);
  if (!c) throw new Error('useConfigDrafts must be used inside <ConfigDraftsProvider>');
  return c;
}
