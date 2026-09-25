import type { ButtonHTMLAttributes, ReactNode } from 'react';

export function cx(...parts: (string | false | null | undefined)[]): string {
  return parts.filter(Boolean).join(' ');
}

export function Card({ children, className }: { children: ReactNode; className?: string }) {
  return (
    <section className={cx('surface rounded-2xl border p-5', className)}>
      {children}
    </section>
  );
}

export function CardTitle({ children, right }: { children: ReactNode; right?: ReactNode }) {
  return (
    <div className="mb-4 flex flex-wrap items-center justify-between gap-2">
      <h2 className="text-base font-semibold text-zinc-100">{children}</h2>
      {right}
    </div>
  );
}

type Tone = 'green' | 'amber' | 'red' | 'zinc' | 'sky';

const toneClasses: Record<Tone, string> = {
  green: 'bg-emerald-500/10 text-emerald-300 ring-emerald-500/30',
  amber: 'bg-amber-500/10 text-amber-300 ring-amber-500/30',
  red: 'bg-red-500/10 text-red-300 ring-red-500/30',
  zinc: 'bg-zinc-500/10 text-zinc-400 ring-zinc-500/30',
  sky: 'bg-sky-500/10 text-sky-300 ring-sky-500/30',
};

export function Badge({ tone = 'zinc', children }: { tone?: Tone; children: ReactNode }) {
  return (
    <span
      className={cx(
        'inline-flex items-center gap-1.5 rounded-full px-2.5 py-0.5 text-xs font-medium ring-1 ring-inset',
        toneClasses[tone],
      )}
    >
      {children}
    </span>
  );
}

export function ConnectionBadge({ connected, hubOnline }: { connected: boolean; hubOnline: boolean }) {
  if (connected)
    return (
      <Badge tone="green">Connected</Badge>
    );
  return <Badge tone="zinc">{hubOnline ? 'Not connected' : 'Hub offline'}</Badge>;
}

type Variant = 'primary' | 'danger' | 'ghost' | 'subtle';

const variantClasses: Record<Variant, string> = {
  primary: 'bg-emerald-500 text-ink hover:bg-emerald-400',
  danger: 'bg-red-600 text-white hover:bg-red-500',
  ghost: 'text-zinc-300 hover:bg-zinc-800',
  subtle: 'bg-zinc-800 text-zinc-100 hover:bg-zinc-700',
};

/** md = every page action; sm = compact (inside menus, small links like "Show more"). */
export function Button({
  variant = 'subtle',
  size = 'md',
  className,
  ...rest
}: ButtonHTMLAttributes<HTMLButtonElement> & { variant?: Variant; size?: 'md' | 'sm' }) {
  return (
    <button
      {...rest}
      className={cx(
        'inline-flex items-center justify-center gap-2 rounded-lg font-semibold transition',
        size === 'sm' ? 'px-3 py-1.5 text-xs' : 'px-3.5 py-2 text-sm',
        'focus-visible:ring-2 focus-visible:ring-emerald-400 focus-visible:outline-none',
        'disabled:cursor-not-allowed disabled:opacity-40',
        variantClasses[variant],
        className,
      )}
    />
  );
}

export function Stat({ label, value, tone }: { label: string; value: ReactNode; tone?: Tone }) {
  return (
    <div className="rounded-xl bg-zinc-950/60 px-3 py-2.5 ring-1 ring-zinc-800">
      <div className="text-xs text-zinc-500">{label}</div>
      <div
        className={cx(
          'mt-0.5 text-sm font-medium tabular-nums',
          tone === 'green' && 'text-emerald-300',
          tone === 'amber' && 'text-amber-300',
          tone === 'red' && 'text-red-300',
          tone === 'sky' && 'text-sky-300',
        )}
      >
        {value}
      </div>
    </div>
  );
}

export function EmptyNote({ children }: { children: ReactNode }) {
  return (
    <p className="rounded-xl border border-dashed border-zinc-800 px-4 py-6 text-center text-sm text-zinc-500">
      {children}
    </p>
  );
}

export function PageHeader({ title, subtitle, right }: { title: string; subtitle?: string; right?: ReactNode }) {
  return (
    <div className="mb-6 flex flex-wrap items-end justify-between gap-3">
      <div>
        <h1 className="text-3xl font-bold tracking-tight">{title}</h1>
        {subtitle && <p className="mt-1 text-sm text-zinc-400">{subtitle}</p>}
      </div>
      {right}
    </div>
  );
}

export function Spinner() {
  return (
    <div className="flex min-h-[40vh] items-center justify-center">
      <div className="h-8 w-8 animate-spin rounded-full border-2 border-zinc-700 border-t-emerald-400" />
    </div>
  );
}
