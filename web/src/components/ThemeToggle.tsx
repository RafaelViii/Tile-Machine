import { useEffect, useState } from 'react';
import { cx } from './ui';

type Theme = 'auto' | 'light' | 'dark';
const KEY = 'tm-theme';

function readTheme(): Theme {
  try {
    const t = localStorage.getItem(KEY);
    return t === 'light' || t === 'dark' ? t : 'auto';
  } catch {
    return 'auto';
  }
}

function applyTheme(t: Theme) {
  const root = document.documentElement;
  if (t === 'auto') root.removeAttribute('data-theme');
  else root.setAttribute('data-theme', t);
  try {
    if (t === 'auto') localStorage.removeItem(KEY);
    else localStorage.setItem(KEY, t);
  } catch {
    /* private mode etc.: the theme still applies for this visit */
  }
}

const OPTIONS: { value: Theme; label: string; title: string }[] = [
  { value: 'auto', label: 'Auto', title: 'Follow this device’s light/dark setting' },
  { value: 'light', label: 'Light', title: 'Light mode' },
  { value: 'dark', label: 'Dark', title: 'Dark mode' },
];

/** Auto / Light / Dark, remembered per browser (index.html applies it before first paint). */
export function ThemeToggle() {
  const [theme, setTheme] = useState<Theme>(readTheme);
  useEffect(() => applyTheme(theme), [theme]);

  return (
    <div role="radiogroup" aria-label="Theme" className="inline-flex rounded-lg bg-zinc-900 p-0.5 ring-1 ring-zinc-800">
      {OPTIONS.map((o) => (
        <button
          key={o.value}
          type="button"
          role="radio"
          aria-checked={theme === o.value}
          title={o.title}
          onClick={() => setTheme(o.value)}
          className={cx(
            'rounded-md px-2.5 py-1 text-xs font-medium transition',
            theme === o.value ? 'bg-zinc-700 text-zinc-50' : 'text-zinc-500 hover:text-zinc-200',
          )}
        >
          {o.label}
        </button>
      ))}
    </div>
  );
}
