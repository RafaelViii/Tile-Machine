import type { InputHTMLAttributes } from 'react';

/** Text/password field with the same look as NumberField (label, rounded-lg field, optional hint). */
export function TextField({
  label,
  hint,
  className,
  ...input
}: InputHTMLAttributes<HTMLInputElement> & { label: string; hint?: string }) {
  return (
    <label className={className ?? 'block'}>
      <span className="text-xs font-medium text-zinc-400">{label}</span>
      <input
        {...input}
        className="mt-1 w-full rounded-lg bg-zinc-950 px-3 py-2 text-sm text-zinc-100 ring-1 ring-zinc-700 outline-none placeholder:text-zinc-600 focus:ring-emerald-500 disabled:opacity-50"
      />
      {hint && <span className="mt-1 block text-xs text-zinc-500">{hint}</span>}
    </label>
  );
}
