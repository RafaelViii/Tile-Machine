import { useCallback, useRef, useState, type FormEvent } from 'react';
import { useAuth } from '../features/auth/auth';
import { useDismiss } from '../shared/hooks/useDismiss';
import { ChevronDown, LogoutIcon } from './icons';
import { ThemeToggle } from './ThemeToggle';
import { Button, cx } from './ui';

const ROLE_LABEL = { superadmin: 'Superadmin', operator: 'Operator', hub: 'Hub' } as const;

/** Round initial button: name + role, theme, change password, sign out. */
export function AccountMenu() {
  const { user, name, role, logout } = useAuth();
  const [open, setOpen] = useState(false);
  const [pwMode, setPwMode] = useState(false);
  const box = useRef<HTMLDivElement>(null);
  const close = useCallback(() => {
    setOpen(false);
    setPwMode(false);
  }, []);
  useDismiss(box, open, close);

  const email = user?.email ?? '';
  const initial = ((name || email)[0] ?? '?').toUpperCase();

  return (
    <div ref={box} className="relative">
      <button
        type="button"
        aria-haspopup="menu"
        aria-expanded={open}
        aria-label="Account menu"
        onClick={() => (open ? close() : setOpen(true))}
        className="flex items-center gap-1 rounded-full p-0.5 pr-1.5 ring-1 ring-zinc-800 transition hover:ring-zinc-600"
      >
        <span className="grid h-8 w-8 place-items-center rounded-full bg-zinc-800 text-sm font-semibold text-zinc-100">
          {initial}
        </span>
        <ChevronDown className={cx('h-3.5 w-3.5 text-zinc-500 transition', open && 'rotate-180')} />
      </button>

      {open && (
        <div role="menu" className="popover absolute top-full right-0 z-40 mt-2 w-72 rounded-xl border p-1">
          <div className="px-2.5 pt-2 pb-2">
            <div className="flex items-center justify-between gap-2">
              <span className="truncate text-sm font-medium text-zinc-100">{name || email}</span>
              {role && <span className="shrink-0 text-xs text-zinc-500">{ROLE_LABEL[role]}</span>}
            </div>
            {name && name !== email && (
              <div className="truncate text-xs text-zinc-500" title={email}>
                {email}
              </div>
            )}
          </div>
          <div className="mx-2.5 h-px bg-zinc-800" />
          {pwMode ? (
            <PasswordForm onDone={() => setPwMode(false)} />
          ) : (
            <>
              <div className="flex items-center justify-between gap-2 px-2.5 py-2">
                <span className="text-sm text-zinc-400">Theme</span>
                <ThemeToggle />
              </div>
              <div className="mx-2.5 mb-1 h-px bg-zinc-800" />
              <button
                type="button"
                role="menuitem"
                onClick={() => setPwMode(true)}
                className="flex w-full items-center gap-2 rounded-md px-2.5 py-2 text-left text-sm text-zinc-300 hover:bg-zinc-800/70"
              >
                Change password
              </button>
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
            </>
          )}
        </div>
      )}
    </div>
  );
}

function PasswordForm({ onDone }: { onDone: () => void }) {
  const { changePassword } = useAuth();
  const [current, setCurrent] = useState('');
  const [next, setNext] = useState('');
  const [again, setAgain] = useState('');
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    if (next.length < 8) return setMsg({ ok: false, text: 'New password: at least 8 characters.' });
    if (next !== again) return setMsg({ ok: false, text: 'The new passwords are not the same.' });
    setBusy(true);
    setMsg(null);
    try {
      await changePassword(current, next);
      setMsg({ ok: true, text: 'Password changed.' });
      setCurrent('');
      setNext('');
      setAgain('');
    } catch (err) {
      const code = err instanceof Error ? err.message : String(err);
      setMsg({
        ok: false,
        text: /invalid-credential|wrong-password/.test(code)
          ? 'Current password is wrong.'
          : /too-many-requests/.test(code)
            ? 'Too many attempts. Wait a minute.'
            : code,
      });
    } finally {
      setBusy(false);
    }
  };

  const field =
    'mt-1 w-full rounded-md bg-zinc-950 px-2.5 py-1.5 text-sm text-zinc-100 ring-1 ring-zinc-700 outline-none focus:ring-emerald-500';
  return (
    <form onSubmit={submit} className="space-y-2 px-2.5 py-2">
      <label className="block text-xs text-zinc-500">
        Current password
        <input type="password" autoComplete="current-password" value={current} onChange={(e) => setCurrent(e.target.value)} className={field} />
      </label>
      <label className="block text-xs text-zinc-500">
        New password
        <input type="password" autoComplete="new-password" value={next} onChange={(e) => setNext(e.target.value)} className={field} />
      </label>
      <label className="block text-xs text-zinc-500">
        New password again
        <input type="password" autoComplete="new-password" value={again} onChange={(e) => setAgain(e.target.value)} className={field} />
      </label>
      {msg && <p className={cx('text-xs', msg.ok ? 'text-emerald-300' : 'text-red-400')}>{msg.text}</p>}
      <div className="flex justify-end gap-2 pt-1">
        <Button type="button" variant="ghost" className="px-2.5 py-1.5 text-xs" onClick={onDone}>
          {msg?.ok ? 'Done' : 'Cancel'}
        </Button>
        <Button type="submit" variant="primary" className="px-2.5 py-1.5 text-xs" disabled={busy || !current || !next}>
          {busy ? 'Saving…' : 'Change'}
        </Button>
      </div>
    </form>
  );
}
