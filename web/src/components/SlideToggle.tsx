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
      className="relative grid w-full max-w-xs grid-cols-2 rounded-md bg-zinc-950 p-1 font-display text-sm font-semibold tracking-wider uppercase ring-1 ring-zinc-700 ring-inset disabled:opacity-50"
    >
      <span
        className={cx(
          'absolute top-1 bottom-1 left-1 w-[calc(50%-0.25rem)] rounded-[4px] bg-emerald-500 shadow-[0_1px_0_0_rgb(0_0_0/0.3)] transition-transform duration-200',
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
