import { useEffect, useRef, useState } from 'react';
import { NavLink, Outlet, useLocation } from 'react-router';
import { useAuth } from '../features/auth/auth';
import { useMachine } from '../shared/machine';
import { endPresence, setPresencePage, startPresence } from '../shared/presence';
import { AccountMenu } from './AccountMenu';
import { CommandButton } from './CommandButton';
import { Footer } from './Footer';
import { HubIcon, LogoMark } from './icons';
import { cx } from './ui';

const links: { to: string; label: string; end?: boolean }[] = [
  { to: '/', label: 'Dashboard', end: true },
  { to: '/shredder', label: 'Shredder' },
  { to: '/containing', label: 'Containing' },
  { to: '/hotpress', label: 'Hot Press' },
  { to: '/events', label: 'Events' },
];
const superLinks: typeof links = [
  { to: '/users', label: 'Users' },
  { to: '/activity', label: 'Activity' },
];

/** Page name shown to the superadmin in "online now". */
export function pageName(pathname: string): string {
  return [...links, ...superLinks].find((l) => (l.to === '/' ? pathname === '/' : pathname.startsWith(l.to)))?.label ?? 'Dashboard';
}

export function Layout() {
  const { hubOnline, loading } = useMachine();
  const { pathname } = useLocation();
  const { user, isSuper } = useAuth();
  const uid = user?.uid;
  const navLinks = isSuper ? [...links, ...superLinks] : links;

  // Online presence (seen by the superadmin). The app is rebuilt per account (App.tsx), so this
  // effect ends the previous account's presence before the next one starts.
  useEffect(() => {
    if (!uid) return;
    startPresence(uid, pageName(window.location.pathname));
    return () => void endPresence();
  }, [uid]);
  useEffect(() => setPresencePage(pageName(pathname)), [pathname]);

  const nav = useRef<HTMLElement>(null);
  const [more, setMore] = useState(false);
  const checkMore = () => {
    const el = nav.current;
    setMore(!!el && el.scrollLeft + el.clientWidth < el.scrollWidth - 2);
  };
  useEffect(() => {
    checkMore();
    window.addEventListener('resize', checkMore);
    return () => window.removeEventListener('resize', checkMore);
  }, []);

  // Each page opens at the top instead of keeping the previous page's scroll.
  // Block body on purpose: newer browsers return a Promise from scrollTo, which React would treat as a cleanup.
  useEffect(() => {
    window.scrollTo(0, 0);
  }, [pathname]);

  return (
    <div className="flex min-h-screen flex-col">
      <header className="bar sticky top-0 z-30 border-b border-zinc-800 backdrop-blur">
        <div className="mx-auto flex max-w-6xl items-center gap-3 px-4 py-3">
          <div className="flex items-center gap-2.5">
            <LogoMark raised className="h-9 w-9" />
            <span className="hidden text-lg font-bold tracking-tight sm:inline">Tile Console</span>
          </div>

          <div className="ml-auto flex items-center gap-3">
            <CommandButton
              moduleId="all"
              type="STOP"
              variant="danger"
              className="h-9"
              disabled={!hubOnline}
              disabledReason="Hub offline: use the physical STOP buttons"
            >
              STOP ALL
            </CommandButton>
            <span className="h-6 w-px bg-zinc-800" aria-hidden />
            <AccountMenu />
          </div>
        </div>

        <div className="mx-auto flex max-w-6xl items-center gap-3 px-4 pb-2">
          {/* On phones the tabs scroll sideways; a fade on the right shows there is more until the end is reached. */}
          <nav
            ref={nav}
            onScroll={checkMore}
            className={cx(
              'flex min-w-0 flex-1 gap-1 overflow-x-auto [scrollbar-width:none]',
              more && '[mask-image:linear-gradient(to_right,black_80%,transparent)]',
            )}
          >
            {navLinks.map((l) => (
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

      <main className="mx-auto w-full max-w-6xl flex-1 px-4 py-6">
        <Outlet />
      </main>
      <Footer />
    </div>
  );
}

/** Quiet hub indicator at the end of the tab row: the hub icon in the status colour (text in the tooltip); one red word only when offline. */
function HubStatus({ online, loading }: { online: boolean; loading: boolean }) {
  const title = online
    ? 'Main hub online'
    : loading
      ? 'Connecting to the hub…'
      : 'Main hub offline: the website cannot reach the machine';
  return (
    <span
      role="status"
      aria-label={title}
      title={title}
      className={cx(
        'flex shrink-0 items-center gap-1.5 text-xs font-medium',
        online ? 'text-emerald-400' : loading ? 'text-zinc-500' : 'text-red-400',
      )}
    >
      <HubIcon className="h-4 w-4" />
      {!online && !loading && <span>Offline</span>}
    </span>
  );
}
