import { useState, type FormEvent } from 'react';
import { Link } from 'react-router';
import { ref, serverTimestamp, update } from 'firebase/database';
import { SlideToggle } from '../../components/SlideToggle';
import { Badge, Button, Card, CardTitle, EmptyNote, PageHeader, cx } from '../../components/ui';
import { createAccountWithoutSwitching, db, setAccountPasswordWithoutSwitching } from '../../lib/firebase';
import { auditEntry } from '../../shared/audit';
import { ago } from '../../shared/format';
import { useValue } from '../../shared/hooks/useValue';
import { useMachine } from '../../shared/machine';
import { PRESENCE_STALE_MS } from '../../shared/presence';
import type { PresenceNode, Role, UserNode } from '../../shared/types/rtdb';
import { useAuth } from '../auth/auth';

export interface Person {
  uid: string;
  name: string;
  email: string;
  role: Role | null;
  online: boolean;
  where: string; // "Shredder page · Edge on Windows" while online
  lastSeen?: number;
}

/** Users + roles + presence, merged. Shared with the Activity page. */
export function usePeople(): { people: Person[] | undefined; now: number } {
  const { now } = useMachine();
  const users = useValue<Record<string, UserNode>>('users').data;
  const roles = useValue<Record<string, Role>>('roles').data;
  const presence = useValue<Record<string, PresenceNode>>('presence').data;
  if (users === undefined || roles === undefined || presence === undefined) return { people: undefined, now };
  const uids = new Set([...Object.keys(users ?? {}), ...Object.keys(roles ?? {})]);
  const people: Person[] = [];
  for (const uid of uids) {
    const role = roles?.[uid] ?? null;
    if (role === 'hub') continue; // the hub ESP32 isn't a person
    const u = users?.[uid];
    const p = presence?.[uid];
    const conns = Object.values(p?.connections ?? {});
    const fresh = typeof p?.lastSeen === 'number' && now - p.lastSeen < PRESENCE_STALE_MS;
    const online = conns.length > 0 && fresh && role !== null;
    people.push({
      uid,
      name: u?.name ?? u?.email ?? uid.slice(0, 8),
      email: u?.email ?? '',
      role,
      online,
      where: online ? conns.map((c) => `${c.page} page · ${c.device}`).join(', ') : '',
      lastSeen: p?.lastSeen,
    });
  }
  const rank = (r: Role | null) => (r === 'superadmin' ? 0 : r === 'operator' ? 1 : 2);
  people.sort((a, b) => rank(a.role) - rank(b.role) || a.name.localeCompare(b.name));
  return { people, now };
}

export function UsersPage() {
  const { people, now } = usePeople();
  const [adding, setAdding] = useState(false);

  return (
    <>
      <PageHeader
        title="Users"
        subtitle="Who can use Tile Console. Only you (superadmin) see this page."
        right={
          !adding && (
            <Button variant="primary" onClick={() => setAdding(true)}>
              Add operator
            </Button>
          )
        }
      />
      {adding && <AddOperator onClose={() => setAdding(false)} />}
      <Card>
        <CardTitle>People</CardTitle>
        {people === undefined ? (
          <p className="text-sm text-zinc-500">Loading…</p>
        ) : people.length === 0 ? (
          <EmptyNote>No users yet.</EmptyNote>
        ) : (
          <ul className="divide-y divide-zinc-800">
            {people.map((p) => (
              <PersonRow key={p.uid} p={p} now={now} />
            ))}
          </ul>
        )}
      </Card>
      <p className="mt-4 text-xs text-zinc-500">
        Operators can do everything on the machine pages, but can't change passwords or add accounts: only you
        can. Turning access off locks their open pages at once. To change an operator's password you need their
        current one (you set it); if nobody knows it, add a new account and turn the old one off.
      </p>
    </>
  );
}

function PersonRow({ p, now }: { p: Person; now: number }) {
  const { user } = useAuth();
  const isMe = p.uid === user?.uid;
  const [renaming, setRenaming] = useState(false);
  const [name, setName] = useState(p.name);
  const [confirmOff, setConfirmOff] = useState(false);
  const [pwOpen, setPwOpen] = useState(false);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);

  const run = async (fn: () => Promise<unknown>) => {
    setBusy(true);
    setErr(null);
    try {
      await fn();
    } catch (e) {
      setErr(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  const setAccess = (on: boolean) =>
    run(() => {
      const [ap, entry] = auditEntry('USER_ACCESS', { summary: `${on ? 'Turned on' : 'Turned off'} access for ${p.name}` });
      return update(ref(db), { [`roles/${p.uid}`]: on ? 'operator' : null, [ap]: entry }).then(() => setConfirmOff(false));
    });

  const saveName = (e: FormEvent) => {
    e.preventDefault();
    const n = name.trim();
    if (!n || n === p.name) return setRenaming(false);
    void run(() => {
      const [ap, entry] = auditEntry('USER_RENAME', { summary: `Renamed ${p.name} to ${n}` });
      return update(ref(db), { [`users/${p.uid}/name`]: n, [ap]: entry }).then(() => setRenaming(false));
    });
  };

  return (
    <li className="flex flex-wrap items-center gap-x-4 gap-y-2 py-3">
      <span
        className={cx(
          'grid h-9 w-9 shrink-0 place-items-center rounded-full text-sm font-semibold',
          p.online ? 'bg-emerald-500/15 text-emerald-300' : 'bg-zinc-800 text-zinc-400',
        )}
        title={p.online ? 'Online' : 'Offline'}
      >
        {(p.name[0] ?? '?').toUpperCase()}
      </span>
      <div className="min-w-0 flex-1">
        {renaming ? (
          <form onSubmit={saveName} className="flex items-center gap-2">
            <input
              autoFocus
              value={name}
              maxLength={40}
              onChange={(e) => setName(e.target.value)}
              className="min-w-0 rounded-md bg-zinc-950 px-2 py-1 text-sm ring-1 ring-zinc-700 outline-none focus:ring-emerald-500"
            />
            <Button type="submit" variant="primary" className="px-2.5 py-1 text-xs" disabled={busy}>
              Save
            </Button>
            <Button type="button" variant="ghost" className="px-2 py-1 text-xs" onClick={() => setRenaming(false)}>
              Cancel
            </Button>
          </form>
        ) : (
          <div className="flex flex-wrap items-center gap-2">
            <span className="font-medium">{p.name}</span>
            {isMe && <span className="text-xs text-zinc-500">(you)</span>}
            <Badge tone={p.role === 'superadmin' ? 'sky' : p.role === 'operator' ? 'green' : 'zinc'}>
              {p.role === 'superadmin' ? 'Superadmin' : p.role === 'operator' ? 'Operator' : 'No access'}
            </Badge>
          </div>
        )}
        <div className="truncate text-xs text-zinc-500">{p.email}</div>
        <div className={cx('text-xs', p.online ? 'text-emerald-300' : 'text-zinc-500')}>
          {p.online ? `Online · ${p.where}` : p.lastSeen ? `Last seen ${ago(p.lastSeen, now)}` : 'Never signed in'}
        </div>
        {err && <div className="text-xs text-red-400">{err}</div>}
        {pwOpen && <OperatorPassword p={p} onClose={() => setPwOpen(false)} />}
      </div>
      <div className="flex flex-wrap items-center gap-2">
        <Link to={`/activity?uid=${p.uid}`} className="rounded-lg px-2.5 py-1.5 text-sm font-medium text-zinc-300 hover:bg-zinc-800">
          Activity
        </Link>
        {!renaming && (
          <Button variant="ghost" onClick={() => setRenaming(true)}>
            Rename
          </Button>
        )}
        {!isMe && p.role !== 'superadmin' && p.email && !pwOpen && (
          <Button variant="ghost" onClick={() => setPwOpen(true)}>
            Password
          </Button>
        )}
        {!isMe && p.role !== 'superadmin' && (
          confirmOff ? (
            <span className="flex items-center gap-2 rounded-lg bg-red-500/10 px-2 py-1 text-sm text-red-300">
              Turn off access?
              <Button variant="danger" className="px-2.5 py-1 text-xs" disabled={busy} onClick={() => setAccess(false)}>
                Turn off
              </Button>
              <Button variant="ghost" className="px-2 py-1 text-xs" onClick={() => setConfirmOff(false)}>
                Cancel
              </Button>
            </span>
          ) : (
            <div className="w-40">
              <SlideToggle
                value={p.role === 'operator' ? 'on' : 'off'}
                left={{ value: 'off', label: 'No access' }}
                right={{ value: 'on', label: 'Access' }}
                disabled={busy}
                onChange={(v) => (v === 'on' ? void setAccess(true) : setConfirmOff(true))}
              />
            </div>
          )
        )}
      </div>
    </li>
  );
}

function AddOperator({ onClose }: { onClose: () => void }) {
  const { user } = useAuth();
  const [name, setName] = useState('');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const [done, setDone] = useState<string | null>(null);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    const n = name.trim();
    const em = email.trim().toLowerCase();
    if (!n) return setErr('Type a name.');
    if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(em)) return setErr('Type a valid email (it can be made up, like ana@tile-machine.local).');
    if (password.length < 8) return setErr('Password: at least 8 characters.');
    setBusy(true);
    setErr(null);
    try {
      const uid = await createAccountWithoutSwitching(em, password);
      const [ap, entry] = auditEntry('USER_ADD', { summary: `Added operator ${n} (${em})` });
      await update(ref(db), {
        [`roles/${uid}`]: 'operator',
        [`users/${uid}`]: { email: em, name: n, createdAt: serverTimestamp(), createdBy: user?.uid ?? '' },
        [ap]: entry,
      });
      setDone(`${n} can now sign in with ${em} and the password you set.`);
      setName('');
      setEmail('');
      setPassword('');
    } catch (e2) {
      const m = e2 instanceof Error ? e2.message : String(e2);
      setErr(
        /email-already-in-use/.test(m)
          ? 'This email already has an account. If it is in the list, turn its access on there.'
          : /weak-password/.test(m)
            ? 'Password too weak: use at least 8 characters.'
            : m,
      );
    } finally {
      setBusy(false);
    }
  };

  const field =
    'mt-1 w-full rounded-lg bg-zinc-950 px-3 py-2 text-sm text-zinc-100 ring-1 ring-zinc-700 outline-none focus:ring-emerald-500';
  return (
    <Card className="mb-6">
      <CardTitle>Add operator</CardTitle>
      <form onSubmit={submit} className="grid gap-4 sm:grid-cols-3">
        <label className="block text-xs text-zinc-400">
          Name
          <input value={name} maxLength={40} onChange={(e) => setName(e.target.value)} className={field} placeholder="Ana" />
        </label>
        <label className="block text-xs text-zinc-400">
          Email (login)
          <input
            value={email}
            autoCapitalize="none"
            autoComplete="off"
            onChange={(e) => setEmail(e.target.value)}
            className={field}
            placeholder="ana@tile-machine.local"
          />
        </label>
        <label className="block text-xs text-zinc-400">
          Password (8+ characters)
          <input
            value={password}
            type="password"
            autoComplete="new-password"
            onChange={(e) => setPassword(e.target.value)}
            className={field}
          />
        </label>
        <div className="flex flex-wrap items-center gap-3 sm:col-span-3">
          <Button type="submit" variant="primary" disabled={busy}>
            {busy ? 'Adding…' : 'Add operator'}
          </Button>
          <Button type="button" variant="ghost" onClick={onClose}>
            Close
          </Button>
          {err && <span className="text-sm text-red-400">{err}</span>}
          {done && <span className="text-sm text-emerald-300">{done}</span>}
        </div>
      </form>
    </Card>
  );
}

/** Superadmin sets an operator's password (needs their current one: see setAccountPasswordWithoutSwitching). */
function OperatorPassword({ p, onClose }: { p: Person; onClose: () => void }) {
  const [current, setCurrent] = useState('');
  const [next, setNext] = useState('');
  const [again, setAgain] = useState('');
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    if (next.length < 8) return setMsg({ ok: false, text: 'New password: at least 8 characters.' });
    if (next !== again) return setMsg({ ok: false, text: 'The new passwords are not the same.' });
    if (next === current) return setMsg({ ok: false, text: 'The new password is the same as the current one.' });
    setBusy(true);
    setMsg(null);
    try {
      await setAccountPasswordWithoutSwitching(p.email, current, next);
      const [ap, entry] = auditEntry('USER_PASSWORD', { summary: `Changed the password of ${p.name}` });
      await update(ref(db), { [ap]: entry }).catch(() => {});
      setMsg({ ok: true, text: `Password changed. ${p.name} must sign in again with the new one.` });
      setCurrent('');
      setNext('');
      setAgain('');
    } catch (e2) {
      const code = e2 instanceof Error ? e2.message : String(e2);
      setMsg({
        ok: false,
        text: /invalid-credential|wrong-password|invalid-login/.test(code)
          ? 'Current password is wrong. If nobody knows it: add a new operator account and turn this one off.'
          : /too-many-requests/.test(code)
            ? 'Too many attempts. Wait a few minutes.'
            : /user-disabled/.test(code)
              ? 'This account is disabled in Firebase.'
              : code,
      });
    } finally {
      setBusy(false);
    }
  };

  const field =
    'mt-1 w-full rounded-lg bg-zinc-950 px-2.5 py-1.5 text-sm text-zinc-100 ring-1 ring-zinc-700 outline-none focus:ring-emerald-500';
  return (
    <form onSubmit={submit} className="mt-3 grid max-w-xl gap-2 rounded-xl bg-zinc-950/60 p-3 ring-1 ring-zinc-800 sm:grid-cols-3">
      <label className="block text-xs text-zinc-400">
        Current password
        <input type="password" autoComplete="off" value={current} onChange={(e) => setCurrent(e.target.value)} className={field} />
      </label>
      <label className="block text-xs text-zinc-400">
        New password (8+)
        <input type="password" autoComplete="new-password" value={next} onChange={(e) => setNext(e.target.value)} className={field} />
      </label>
      <label className="block text-xs text-zinc-400">
        New password again
        <input type="password" autoComplete="new-password" value={again} onChange={(e) => setAgain(e.target.value)} className={field} />
      </label>
      <div className="flex flex-wrap items-center gap-2 sm:col-span-3">
        <Button type="submit" variant="primary" className="px-3 py-1.5 text-xs" disabled={busy || !current || !next}>
          {busy ? 'Changing…' : 'Change password'}
        </Button>
        <Button type="button" variant="ghost" className="px-3 py-1.5 text-xs" onClick={onClose}>
          {msg?.ok ? 'Done' : 'Cancel'}
        </Button>
        {msg && <span className={cx('text-xs', msg.ok ? 'text-emerald-300' : 'text-red-400')}>{msg.text}</span>}
      </div>
    </form>
  );
}
