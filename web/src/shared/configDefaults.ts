// Defaults and allowed ranges for module configs.
// Mirrors docs/PROTOCOL.md §6. The firmware rejects values outside these ranges.
import type {
  ConfigByModule,
  ContainingConfig,
  HotpressConfig,
  ModuleId,
  ShredderConfig,
} from './types/rtdb';

export const LIMITS = {
  shredder: {
    autoStartDelayMs: [2000, 30000],
    autoEmptyStopDelayMs: [0, 10000],
    manualConfirmTimeoutMs: [3000, 60000],
    irDebounceMs: [20, 2000],
    buzzerVolumePct: [0, 100],
    switchDebounceMs: [20, 2000],
    buttonDebounceMs: [10, 500],
  },
  containing: {
    timeTableMs: [1000, 600000],
    maxDispenseMs: [10000, 600000],
    jamTimeoutMs: [2000, 60000],
    toleranceG: [0, 500],
    runTimeMs: [1000, 600000],
    stopUs: [1000, 2000],
    runUs: [500, 2500],
  },
  hotpress: {
    buttonDebounceMs: [20, 2000],
    selectorDebounceMs: [20, 2000],
  },
} as const;

export const DEFAULT_SHREDDER: ShredderConfig = {
  version: 0,
  autoStartDelayMs: 5000,
  autoEmptyStopDelayMs: 1500,
  manualConfirmTimeoutMs: 15000,
  irDebounceMs: 200,
  buzzerVolumePct: 100,
  switchDebounceMs: 250,
  buttonDebounceMs: 50,
};

export const DEFAULT_CONTAINING: ContainingConfig = {
  version: 0,
  raw: [0, 1].map(() => ({
    mode: 'LOADCELL' as const,
    timeTableMs: [10000, 20000, 30000, 40000, 50000],
    maxDispenseMs: 120000,
    jamTimeoutMs: 10000,
    toleranceG: 50,
  })),
  mixed: [0, 1].map(() => ({ mode: 'MANUAL' as const, runTimeMs: 10000 })),
  servo: Array.from({ length: 8 }, () => ({ stopUs: 1500, runUs: 1300 })),
};

export const DEFAULT_HOTPRESS: HotpressConfig = {
  version: 0,
  autoModeBehaviour: 0,
  buttonDebounceMs: 200,
  selectorDebounceMs: 150,
};

/** Fill anything missing from a stored config with defaults (arrays padded to full length). */
export function normalizeConfig<M extends ModuleId>(id: M, stored: unknown): ConfigByModule[M] {
  // editedBy / editedAt describe the last save, not a setting: keep them out of forms, presets and diffs.
  const { editedBy: _by, editedAt: _at, ...s } = (stored ?? {}) as Record<string, unknown>;
  if (id === 'shredder') {
    return { ...DEFAULT_SHREDDER, ...s } as ConfigByModule[M];
  }
  if (id === 'hotpress') {
    return { ...DEFAULT_HOTPRESS, ...s } as ConfigByModule[M];
  }
  const c = s as Partial<ContainingConfig>;
  const d = DEFAULT_CONTAINING;
  const out: ContainingConfig = {
    version: typeof c.version === 'number' ? c.version : 0,
    raw: d.raw.map((def, i) => {
      const r = c.raw?.[i];
      return {
        ...def,
        ...r,
        timeTableMs: def.timeTableMs.map((t, k) => r?.timeTableMs?.[k] ?? t),
      };
    }),
    mixed: d.mixed.map((def, i) => ({ ...def, ...c.mixed?.[i] })),
    servo: d.servo.map((def, i) => ({ ...def, ...c.servo?.[i] })),
  };
  return out as ConfigByModule[M];
}

export function clamp(v: number, [min, max]: readonly [number, number]): number {
  if (!Number.isFinite(v)) return min;
  return Math.min(max, Math.max(min, v));
}
