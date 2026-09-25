import { useEffect, useState } from 'react';

/**
 * Number input that edits a value stored in `scale` units (e.g. ms shown as seconds with scale=1000).
 * The value is clamped to [min, max] (stored units) on blur.
 */
export function NumberField({
  label,
  hint,
  value,
  onChange,
  min,
  max,
  scale = 1,
  step = 1,
  unit,
  disabled,
}: {
  label: string;
  hint?: string;
  value: number;
  onChange: (v: number) => void;
  min: number;
  max: number;
  scale?: number;
  step?: number;
  unit?: string;
  disabled?: boolean;
}) {
  const shown = +(value / scale).toFixed(3);
  const [text, setText] = useState(String(shown));

  useEffect(() => setText(String(shown)), [shown]);

  const commit = () => {
    const n = Number(text);
    const stored = Number.isFinite(n) ? Math.round(n * scale) : value;
    const clamped = Math.min(max, Math.max(min, stored));
    onChange(clamped);
    setText(String(+(clamped / scale).toFixed(3)));
  };

  return (
    <label className="block">
      <span className="text-xs font-medium text-zinc-400">{label}</span>
      <div className="mt-1 flex items-center rounded-lg bg-zinc-950 ring-1 ring-zinc-700 focus-within:ring-emerald-500">
        <input
          type="number"
          inputMode="decimal"
          className="w-full min-w-0 bg-transparent px-3 py-2 text-sm tabular-nums outline-none disabled:opacity-50"
          value={text}
          step={step}
          min={min / scale}
          max={max / scale}
          disabled={disabled}
          onChange={(e) => setText(e.target.value)}
          onBlur={commit}
          onKeyDown={(e) => e.key === 'Enter' && (e.target as HTMLInputElement).blur()}
        />
        {unit && <span className="pr-3 text-xs text-zinc-500">{unit}</span>}
      </div>
      {hint && (
        <span className="mt-1 block text-xs text-zinc-500">
          {hint} · range {min / scale}–{max / scale}
          {unit ? ` ${unit}` : ''}
        </span>
      )}
    </label>
  );
}
