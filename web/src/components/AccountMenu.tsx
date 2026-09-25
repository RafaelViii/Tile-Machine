import { useCallback, useRef, useState } from 'react';
import { useAuth } from '../features/auth/auth';
import { useDismiss } from '../shared/hooks/useDismiss';
import { ChevronDown, LogoutIcon } from './icons';
import { ThemeToggle } from './ThemeToggle';
import { cx } from './ui';

/** Round initial button with the signed-in email, theme choice and Sign out. */
export function AccountMenu() {
  const { user, logout } = useAuth();
  const [open, setOpen] = useState(false);
  const box = useRef<HTMLDivElement>(null);
  const close = useCallback(() => setOpen(false), []);
  useDismiss(box, open, close);

  const email = user?.email ?? '';
  const initial = (email[0] ?? '?').toUpperCase();

  return (
    <div ref={box} className="relative">
      <button
        type="button"
        aria-haspopup="menu"
        aria-expanded={open}
        aria-label="Account menu"
        onClick={() => setOpen((o) => !o)}
        className="flex items-center gap-1 rounded-full p-0.5 pr-1.5 ring-1 ring-zinc-800 transition hover:ring-zinc-600"
      >
        <span className="grid h-8 w-8 place-items-center rounded-full bg-zinc-800 text-sm font-semibold text-zinc-100">
          {initial}
        </span>
        <ChevronDown className={cx('h-3.5 w-3.5 text-zinc-500 transition', open && 'rotate-180')} />
      </button>

      {open && (
        <div role="menu" className="popover absolute top-full right-0 z-40 mt-2 w-64 rounded-xl border p-1">
          <div className="px-2.5 pt-2 pb-2">
            <div className="text-xs text-zinc-500">Signed in as</div>
            <div className="truncate text-sm text-zinc-200" title={email}>
              {email}
            </div>
          </div>
          <div className="mx-2.5 h-px bg-zinc-800" />
          <div className="flex items-center justify-between gap-2 px-2.5 py-2">
            <span className="text-sm text-zinc-400">Theme</span>
            <ThemeToggle />
          </div>
          <div className="mx-2.5 mb-1 h-px bg-zinc-800" />
          <button
            type="button"
            role="menuitem"
            onClick={() => {
              close();
              void logout();
            }}
            className="flex w-full items-center gap-2 rounded-md px-2.5 py-2 text-left text-sm text-zinc-300 hover:bg-zinc-800/70"
          >
            <LogoutIcon className="h-4 w-4" />
            Sign out
          </button>
        </div>
      )}
    </div>
  );
}
