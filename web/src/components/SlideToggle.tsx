import { cx } from './ui';

interface Option<T extends string> {
  value: T;
  label: string;
}

/** Two-position slide switch (e.g. Loadcell ↔ Time). */
export function SlideToggle<T extends string>({
  left,
  right,
  value,
  onChange,
  disabled,
}: {
  left: Option<T>;
  right: Option<T>;
  value: T;
  onChange: (v: T) => void;
  disabled?: boolean;
}) {
  const isRight = value === right.value;
  return (
    <button
      type="button"
      role="switch"
      aria-checked={isRight}
      disabled={disabled}
      onClick={() => onChange(isRight ? left.value : right.value)}
      className="relative grid w-full max-w-xs grid-cols-2 rounded-lg bg-zinc-950 p-0.5 text-xs font-semibold ring-1 ring-zinc-700 disabled:opacity-50"
    >
      <span
        className={cx(
          'absolute top-0.5 bottom-0.5 left-0.5 w-[calc(50%-0.125rem)] rounded-md bg-emerald-500 transition-transform duration-200',
          isRight && 'translate-x-full',
        )}
      />
      <span className={cx('relative z-10 py-1.5 transition-colors', !isRight ? 'text-ink' : 'text-zinc-400')}>
        {left.label}
      </span>
      <span className={cx('relative z-10 py-1.5 transition-colors', isRight ? 'text-ink' : 'text-zinc-400')}>
        {right.label}
      </span>
    </button>
  );
}
