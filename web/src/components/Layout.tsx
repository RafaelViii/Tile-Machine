import { useEffect } from 'react';
import { NavLink, Outlet, useLocation } from 'react-router';
import { useMachine } from '../shared/machine';
import { AccountMenu } from './AccountMenu';
import { CommandButton } from './CommandButton';
import { HubIcon } from './icons';
import { Badge, StatusDot, cx } from './ui';

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
            <img src="/favicon.svg" alt="" className="h-7 w-7" />
            <span className="hidden text-base font-bold tracking-tight sm:inline">Tile Machine</span>
          </div>

          <Badge tone={hubOnline ? 'green' : loading ? 'zinc' : 'red'}>
            <HubIcon className="h-3.5 w-3.5" />
            {loading ? 'Hub…' : hubOnline ? 'Hub online' : 'Hub offline'}
            {hubOnline && <StatusDot on />}
          </Badge>

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

        <nav className="mx-auto flex max-w-6xl gap-1 overflow-x-auto px-4 pb-2">
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
      </header>

      <main className="mx-auto max-w-6xl px-4 py-6">
        <Outlet />
      </main>
    </div>
  );
}
