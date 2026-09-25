import { useEffect } from 'react';
import { NavLink, Outlet, useLocation } from 'react-router';
import { useMachine } from '../shared/machine';
import { AccountMenu } from './AccountMenu';
import { CommandButton } from './CommandButton';
import { HubIcon, LogoMark } from './icons';
import { cx } from './ui';

const links = [
  { to: '/', label: 'Dashboard', end: true },
  { to: '/shredder', label: 'Shredder' },
  { to: '/containing', label: 'Containing' },
  { to: '/hotpress', label: 'Hot Press' },
  { to: '/events', label: 'Events' },
];

export function Layout() {
  const { hubOnline, loading } = useMachine();
  const { pathname } = useLocation();

  // Each page opens at the top instead of keeping the previous page's scroll.
  // Block body on purpose: newer browsers return a Promise from scrollTo, which React would treat as a cleanup.
  useEffect(() => {
    window.scrollTo(0, 0);
  }, [pathname]);

  return (
    <div className="min-h-screen">
      <header className="bar sticky top-0 z-30 border-b border-zinc-800 backdrop-blur">
        <div className="mx-auto flex max-w-6xl items-center gap-3 px-4 py-3">
          <div className="flex items-center gap-2.5">
            <LogoMark raised className="h-9 w-9" />
            <span className="hidden text-lg font-bold tracking-tight sm:inline">Tile Machine</span>
          </div>

          <div className="ml-auto flex items-center gap-3">
            <CommandButton
              moduleId="all"
              type="STOP"
              variant="danger"
              floatingStatus
              className="h-9 tracking-wide"
              disabled={!hubOnline}
              disabledReason="Hub offline: use the physical STOP buttons"
            >
              <span className="h-2.5 w-2.5 rounded-[2px] bg-current" aria-hidden />
              STOP ALL
            </CommandButton>
            <span className="h-6 w-px bg-zinc-800" aria-hidden />
            <AccountMenu />
          </div>
        </div>

        <div className="mx-auto flex max-w-6xl items-center gap-3 px-4 pb-2">
          <nav className="flex min-w-0 flex-1 gap-1 overflow-x-auto">
            {links.map((l) => (
              <NavLink
                key={l.to}
                to={l.to}
                end={l.end}
                className={({ isActive }) =>
                  cx(
                    'rounded-lg px-3 py-1.5 text-sm font-medium whitespace-nowrap transition',
                    isActive ? 'bg-zinc-800 text-zinc-50' : 'text-zinc-400 hover:bg-zinc-900 hover:text-zinc-200',
                  )
                }
              >
                {l.label}
              </NavLink>
            ))}
          </nav>
          <HubStatus online={hubOnline} loading={loading} />
        </div>
      </header>

      <main className="mx-auto max-w-6xl px-4 py-6">
        <Outlet />
      </main>
    </div>
  );
}

/** Quiet hub indicator at the end of the tab row: dot + short text, red only when something is wrong. */
function HubStatus({ online, loading }: { online: boolean; loading: boolean }) {
  const text = loading ? 'Connecting…' : online ? 'Hub online' : 'Hub offline';
  return (
    <span
      role="status"
      title={online ? 'Main hub is online' : loading ? 'Connecting to the hub' : 'Main hub is offline: the website cannot reach the machine'}
      className={cx(
        'flex shrink-0 items-center gap-1.5 text-xs font-medium',
        online ? 'text-zinc-400' : loading ? 'text-zinc-500' : 'text-red-400',
      )}
    >
      <HubIcon className="h-3.5 w-3.5" />
      <span className={cx('h-2 w-2 rounded-full', online ? 'bg-emerald-400' : loading ? 'bg-zinc-600' : 'bg-red-500')} />
      <span className={online ? 'hidden sm:inline' : undefined}>{text}</span>
    </span>
  );
}
