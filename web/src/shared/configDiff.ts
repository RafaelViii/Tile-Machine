import type { ModuleId } from './types/rtdb';

export interface ConfigChange {
  label: string;
  from: string;
  to: string;
}

const LABELS: Record<ModuleId, Record<string, string>> = {
  shredder: {
    autoStartDelayMs: 'AUTO: warning before start',
    autoEmptyStopDelayMs: 'AUTO: empty time before stop',
    manualConfirmTimeoutMs: 'MANUAL: time to press START again',
    irDebounceMs: 'IR sensor debounce',
    buzzerVolumePct: 'Buzzer volume',
    switchDebounceMs: '3-way switch debounce',
    buttonDebounceMs: 'START / STOP button debounce',
  },
  containing: {
    mode: 'Mode',
    maxDispenseMs: 'Safety max time',
    jamTimeoutMs: 'Jam if no drop for',
    toleranceG: 'Tolerance',
    runTimeMs: 'Run time',
    stopUs: 'Stop pulse',
    runUs: 'Run pulse',
  },
  hotpress: {
    autoModeBehaviour: 'AUTO behaviour',
    buttonDebounceMs: 'ON button debounce',
    selectorDebounceMs: 'Selector debounce',
  },
};

const GROUPS: Record<string, (i: number) => string> = {
  raw: (i) => ['Raw HDPE', 'Raw PP'][i] ?? `Raw ${i + 1}`,
  mixed: (i) => ['Mixed HDPE', 'Mixed PP'][i] ?? `Mixed ${i + 1}`,
  servo: (i) => `Servo ch ${i}`,
};

function fmt(key: string, v: unknown): string {
  if (typeof v !== 'number') return v === undefined ? '—' : String(v);
  if (key.endsWith('Ms')) return v >= 1000 && v % 100 === 0 ? `${v / 1000} s` : `${v} ms`;
  if (key.endsWith('Pct')) return `${v}%`;
  if (key.endsWith('G')) return `${v} g`;
  if (key.endsWith('Us')) return `${v} µs`;
  return String(v);
}

/** Every leaf value that differs between two configs of one module (ignores `version`). */
export function diffConfig(id: ModuleId, from: unknown, to: unknown): ConfigChange[] {
  const out: ConfigChange[] = [];
  const walk = (a: unknown, b: unknown, key: string, prefix: string[]) => {
    const isObj = (x: unknown) => x !== null && typeof x === 'object';
    if (isObj(a) || isObj(b)) {
      const A = (a ?? {}) as Record<string, unknown>;
      const B = (b ?? {}) as Record<string, unknown>;
      const arr = Array.isArray(a) || Array.isArray(b);
      const keys = arr
        ? Array.from({ length: Math.max((a as unknown[] | undefined)?.length ?? 0, (b as unknown[] | undefined)?.length ?? 0) }, (_, i) => String(i))
        : [...new Set([...Object.keys(A), ...Object.keys(B)])].filter((k) => !(prefix.length === 0 && k === 'version'));
      for (const k of keys) {
        if (!arr) walk(A[k], B[k], k, prefix);
        else if (GROUPS[key]) walk(A[k], B[k], key, [...prefix, GROUPS[key](Number(k))]);
        else if (key === 'timeTableMs') walk(A[k], B[k], key, [...prefix, `time for ${Number(k) + 1} kg`]);
        else walk(A[k], B[k], key, [...prefix, `#${Number(k) + 1}`]);
      }
      return;
    }
    if (a === b) return;
    const name = key === 'timeTableMs' ? '' : (LABELS[id][key] ?? key);
    out.push({ label: [...prefix, name].filter(Boolean).join(' · '), from: fmt(key, a), to: fmt(key, b) });
  };
  walk(from, to, '', []);
  return out;
}
