import { useEffect } from 'react';
import { NavLink, Outlet, useLocation } from 'react-router';
import { useAuth } from '../features/auth/auth';
import { useMachine } from '../shared/machine';
import { CommandButton } from './CommandButton';
import { HubIcon } from './icons';
import { ThemeToggle } from './ThemeToggle';
import { Badge, Button, Lamp, cx } from './ui';

const links = [
  { to: '/', label: 'Dashboard', end: true },
  { to: '/shredder', label: 'Shredder' },
  { to: '/containing', label: 'Containing' },
  { to: '/hotpress', label: 'Hot Press' },
  { to: '/events', label: 'Events' },
];

export function Layout() {
  const { user, logout } = useAuth();
  const { hubOnline, loading } = useMachine();
  const { pathname } = useLocation();

  // Each page opens at the top. Block body on purpose: newer browsers return a Promise from
  // scrollTo, which React would treat as a cleanup.
  useEffect(() => {
    window.scrollTo(0, 0);
  }, [pathname]);

  return (
    <div className="min-h-screen">
      <header className="sticky top-0 z-30 border-b border-zinc-800 bg-zinc-900/95 backdrop-blur">
        <div className="mx-auto flex max-w-6xl flex-wrap items-center gap-3 px-4 py-3">
          <div className="flex items-center gap-2.5">
            <img src="/favicon.svg" alt="" className="h-8 w-8" />
            <div className="leading-none">
              <div className="font-display text-xl font-bold tracking-[0.08em] uppercase">Tile Machine</div>
              <div className="font-display text-[11px] font-semibold tracking-[0.2em] text-zinc-500 uppercase">
                Control panel
              </div>
            </div>
          </div>

          <Badge tone={hubOnline ? 'green' : loading ? 'zinc' : 'red'}>
            <Lamp state={hubOnline ? 'on' : loading ? 'off' : 'fault'} />
            <HubIcon className="h-3.5 w-3.5" />
            {loading ? 'Hub…' : hubOnline ? 'Hub online' : 'Hub offline'}
          </Badge>

          <div className="ml-auto flex flex-wrap items-center gap-2">
            <ThemeToggle />
            <div className={cx('rounded-md p-[3px]', hubOnline ? 'hazard-stripe' : 'bg-zinc-700')}>
              <CommandButton
                moduleId="all"
                type="STOP"
                variant="danger"
                disabled={!hubOnline}
                disabledReason="Hub offline: use the physical STOP buttons"
                className="rounded-[4px]"
                floatingStatus
              >
                ■ Stop all
              </CommandButton>
            </div>
            <span className="hidden font-mono text-xs text-zinc-500 lg:inline">{user?.email}</span>
            <Button variant="ghost" onClick={logout}>
              Sign out
            </Button>
          </div>
        </div>

        <nav className="mx-auto flex max-w-6xl gap-1 overflow-x-auto px-4">
          {links.map((l) => (
            <NavLink
              key={l.to}
              to={l.to}
              end={l.end}
              className={({ isActive }) =>
                cx(
                  'border-b-[3px] px-3 pt-1 pb-2 font-display text-[15px] font-semibold tracking-wider whitespace-nowrap uppercase transition',
                  isActive
                    ? 'border-hazard text-zinc-50'
                    : 'border-transparent text-zinc-500 hover:border-zinc-700 hover:text-zinc-200',
                )
              }
            >
              {l.label}
            </NavLink>
          ))}
        </nav>
        <div className="hazard-stripe h-[3px] opacity-80" aria-hidden />
      </header>

      <main className="mx-auto max-w-6xl px-4 py-6">
        <Outlet />
      </main>
    </div>
  );
}
