import { useState, type FormEvent, type ReactNode } from 'react';
import { Link } from 'react-router';
import { ref, serverTimestamp, update } from 'firebase/database';
import { SlideToggle } from '../../components/SlideToggle';
import { TextField } from '../../components/TextField';
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

type Panel = 'rename' | 'password' | 'access' | null;

function PersonRow({ p, now }: { p: Person; now: number }) {
  const { user } = useAuth();
  const isMe = p.uid === user?.uid;
  const manageable = !isMe && p.role !== 'superadmin';
  const [panel, setPanel] = useState<Panel>(null);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const toggle = (x: Panel) => setPanel((cur) => (cur === x ? null : x));

  const setAccess = async (on: boolean) => {
    setBusy(true);
    setErr(null);
    try {
      const [ap, entry] = auditEntry('USER_ACCESS', { summary: `${on ? 'Turned on' : 'Turned off'} access for ${p.name}` });
      await update(ref(db), { [`roles/${p.uid}`]: on ? 'operator' : null, [ap]: entry });
      setPanel(null);
    } catch (e) {
      setErr(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <li className="py-3">
      <div className="flex flex-wrap items-center gap-x-4 gap-y-2">
        <span
          className={cx(
            'grid h-9 w-9 shrink-0 place-items-center rounded-full text-sm font-semibold',
            p.online ? 'bg-emerald-500/15 text-emerald-300' : 'bg-zinc-800 text-zinc-400',
          )}
          title={p.online ? 'Online' : 'Offline'}
        >
          {(p.name[0] ?? '?').toUpperCase()}
        </span>
        {/* min width: on phones the buttons wrap below instead of squeezing the name */}
        <div className="min-w-[12rem] flex-1">
          <div className="flex flex-wrap items-center gap-2">
            <span className="font-medium">{p.name}</span>
            {isMe && <span className="text-xs text-zinc-500">(you)</span>}
            <Badge tone={p.role === 'superadmin' ? 'sky' : p.role === 'operator' ? 'green' : 'zinc'}>
              {p.role === 'superadmin' ? 'Superadmin' : p.role === 'operator' ? 'Operator' : 'No access'}
            </Badge>
          </div>
          <div className="truncate text-xs text-zinc-500">{p.email}</div>
          <div className={cx('text-xs', p.online ? 'text-emerald-300' : 'text-zinc-500')}>
            {p.online ? `Online · ${p.where}` : p.lastSeen ? `Last seen ${ago(p.lastSeen, now)}` : 'Never signed in'}
          </div>
        </div>
        <div className="flex flex-wrap items-center gap-1 max-sm:ml-11">
          <Link
            to={`/activity?uid=${p.uid}`}
            className="rounded-lg px-3.5 py-2 text-sm font-semibold text-zinc-300 transition hover:bg-zinc-800"
          >
            Activity
          </Link>
          <Button variant="ghost" onClick={() => toggle('rename')} aria-expanded={panel === 'rename'}>
            Rename
          </Button>
          {manageable && p.email && (
            <Button variant="ghost" onClick={() => toggle('password')} aria-expanded={panel === 'password'}>
              Reset password
            </Button>
          )}
          {manageable && (
            <div className="ml-1 w-40">
              <SlideToggle
                value={p.role === 'operator' ? 'on' : 'off'}
                left={{ value: 'off', label: 'No access' }}
                right={{ value: 'on', label: 'Access' }}
                disabled={busy}
                onChange={(v) => (v === 'on' ? void setAccess(true) : setPanel('access'))}
              />
            </div>
          )}
        </div>
      </div>

      {/* One panel at a time, below the row (same pattern as the Devices list). */}
      {panel && (
        <div className="mt-3 sm:ml-13">
          {panel === 'rename' && <RenamePanel p={p} onClose={() => setPanel(null)} />}
          {panel === 'password' && <PasswordPanel p={p} onClose={() => setPanel(null)} />}
          {panel === 'access' && (
            <PanelBox
              title={`Turn off access for ${p.name}?`}
              text="Their open pages lock at once and they can't sign in to Tile Console until you turn it back on."
              footer={
                <>
                  {err && <span className="mr-auto text-xs text-red-400">{err}</span>}
                  <Button variant="ghost" onClick={() => setPanel(null)}>
                    Cancel
                  </Button>
                  <Button variant="danger" disabled={busy} onClick={() => void setAccess(false)}>
                    {busy ? 'Turning off…' : 'Turn off access'}
                  </Button>
                </>
              }
            />
          )}
        </div>
      )}
    </li>
  );
}

/** Inner block used by every panel: title, short explanation, optional fields, buttons on the right. */
function PanelBox({
  title,
  text,
  children,
  footer,
  onSubmit,
}: {
  title: string;
  text?: string;
  children?: ReactNode;
  footer: ReactNode;
  onSubmit?: (e: FormEvent) => void;
}) {
  const body = (
    <>
      <div className="text-sm font-semibold">{title}</div>
      {text && <p className="mt-0.5 text-xs text-zinc-500">{text}</p>}
      {children && <div className="mt-4">{children}</div>}
      <div className="mt-4 flex flex-wrap items-center justify-end gap-2">{footer}</div>
    </>
  );
  const cls = 'max-w-3xl rounded-xl bg-zinc-950/60 p-4 ring-1 ring-zinc-800';
  return onSubmit ? (
    <form onSubmit={onSubmit} className={cls}>
      {body}
    </form>
  ) : (
    <div className={cls}>{body}</div>
  );
}

function Msg({ m }: { m: { ok: boolean; text: string } | null }) {
  if (!m) return null;
  return <span className={cx('mr-auto text-xs', m.ok ? 'text-emerald-300' : 'text-red-400')}>{m.text}</span>;
}

function RenamePanel({ p, onClose }: { p: Person; onClose: () => void }) {
  const [name, setName] = useState(p.name);
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const submit = async (e: FormEvent) => {
    e.preventDefault();
    const n = name.trim();
    if (!n) return setMsg({ ok: false, text: 'Type a name.' });
    if (n === p.name) return onClose();
    setBusy(true);
    try {
      const [ap, entry] = auditEntry('USER_RENAME', { summary: `Renamed ${p.name} to ${n}` });
      await update(ref(db), { [`users/${p.uid}/name`]: n, [ap]: entry });
      onClose();
    } catch (e2) {
      setMsg({ ok: false, text: e2 instanceof Error ? e2.message : String(e2) });
      setBusy(false);
    }
  };
  return (
    <PanelBox
      title="Rename"
      text="The name shown in Users, Activity and the account menu. The sign-in email stays the same."
      onSubmit={submit}
      footer={
        <>
          <Msg m={msg} />
          <Button type="button" variant="ghost" onClick={onClose}>
            Cancel
          </Button>
          <Button type="submit" variant="primary" disabled={busy}>
            {busy ? 'Saving…' : 'Save name'}
          </Button>
        </>
      }
    >
      <TextField
        label="Name"
        value={name}
        maxLength={40}
        autoFocus
        onChange={(e) => setName(e.target.value)}
        className="block max-w-xs"
      />
    </PanelBox>
  );
}

/** Superadmin resets an operator's password (needs the current one: see setAccountPasswordWithoutSwitching). */
function PasswordPanel({ p, onClose }: { p: Person; onClose: () => void }) {
  const [current, setCurrent] = useState('');
  const [next, setNext] = useState('');
  const [again, setAgain] = useState('');
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    if (!current) return setMsg({ ok: false, text: 'Type their current password.' });
    if (next.length < 8) return setMsg({ ok: false, text: 'The new password needs at least 8 characters.' });
    if (next !== again) return setMsg({ ok: false, text: "The new passwords don't match." });
    if (next === current) return setMsg({ ok: false, text: 'The new password is the same as the current one.' });
    setBusy(true);
    setMsg(null);
    try {
      await setAccountPasswordWithoutSwitching(p.email, current, next);
      const [ap, entry] = auditEntry('USER_PASSWORD', { summary: `Reset the password of ${p.name}` });
      await update(ref(db), { [ap]: entry }).catch(() => {});
      setMsg({ ok: true, text: `Done. ${p.name} signs in with the new password from now on.` });
      setCurrent('');
      setNext('');
      setAgain('');
    } catch (e2) {
      const code = e2 instanceof Error ? e2.message : String(e2);
      setMsg({
        ok: false,
        text: /invalid-credential|wrong-password|invalid-login/.test(code)
          ? 'The current password is wrong. If nobody knows it, add a new account and turn this one off.'
          : /too-many-requests/.test(code)
            ? 'Too many attempts. Wait a few minutes and try again.'
            : code,
      });
    } finally {
      setBusy(false);
    }
  };

  return (
    <PanelBox
      title={`Reset password for ${p.name}`}
      text={`Type the password you gave ${p.name}, then the new one. They'll need to sign in again with it.`}
      onSubmit={submit}
      footer={
        <>
          <Msg m={msg} />
          <Button type="button" variant="ghost" onClick={onClose}>
            {msg?.ok ? 'Close' : 'Cancel'}
          </Button>
          {!msg?.ok && (
            <Button type="submit" variant="primary" disabled={busy}>
              {busy ? 'Resetting…' : 'Reset password'}
            </Button>
          )}
        </>
      }
    >
      <div className="grid gap-4 sm:grid-cols-3">
        <TextField
          label="Current password"
          type="password"
          autoComplete="off"
          value={current}
          onChange={(e) => setCurrent(e.target.value)}
        />
        <TextField
          label="New password"
          hint="At least 8 characters"
          type="password"
          autoComplete="new-password"
          value={next}
          onChange={(e) => setNext(e.target.value)}
        />
        <TextField
          label="Repeat new password"
          type="password"
          autoComplete="new-password"
          value={again}
          onChange={(e) => setAgain(e.target.value)}
        />
      </div>
    </PanelBox>
  );
}

function AddOperator({ onClose }: { onClose: () => void }) {
  const { user } = useAuth();
  const [name, setName] = useState('');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    const n = name.trim();
    const em = email.trim().toLowerCase();
    if (!n) return setMsg({ ok: false, text: 'Type a name.' });
    if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(em))
      return setMsg({ ok: false, text: 'Type a valid email. It can be made up, like ana@tile-machine.local.' });
    if (password.length < 8) return setMsg({ ok: false, text: 'The password needs at least 8 characters.' });
    setBusy(true);
    setMsg(null);
    try {
      const uid = await createAccountWithoutSwitching(em, password);
      const [ap, entry] = auditEntry('USER_ADD', { summary: `Added operator ${n} (${em})` });
      await update(ref(db), {
        [`roles/${uid}`]: 'operator',
        [`users/${uid}`]: { email: em, name: n, createdAt: serverTimestamp(), createdBy: user?.uid ?? '' },
        [ap]: entry,
      });
      setMsg({ ok: true, text: `${n} can now sign in with ${em} and the password you set.` });
      setName('');
      setEmail('');
      setPassword('');
    } catch (e2) {
      const m = e2 instanceof Error ? e2.message : String(e2);
      setMsg({
        ok: false,
        text: /email-already-in-use/.test(m)
          ? 'This email already has an account. If it is in the list, turn its access on there.'
          : /weak-password/.test(m)
            ? 'That password is too weak. Use at least 8 characters.'
            : m,
      });
    } finally {
      setBusy(false);
    }
  };

  return (
    <Card className="mb-6">
      <CardTitle>Add operator</CardTitle>
      <form onSubmit={submit}>
        <div className="grid gap-4 sm:grid-cols-3">
          <TextField label="Name" value={name} maxLength={40} placeholder="Ana" onChange={(e) => setName(e.target.value)} />
          <TextField
            label="Sign-in email"
            hint="Can be made up, like ana@tile-machine.local"
            value={email}
            autoCapitalize="none"
            autoComplete="off"
            placeholder="ana@tile-machine.local"
            onChange={(e) => setEmail(e.target.value)}
          />
          <TextField
            label="Password"
            hint="At least 8 characters. Give it to the operator."
            type="password"
            autoComplete="new-password"
            value={password}
            onChange={(e) => setPassword(e.target.value)}
          />
        </div>
        <div className="mt-5 flex flex-wrap items-center justify-end gap-2">
          <Msg m={msg} />
          <Button type="button" variant="ghost" onClick={onClose}>
            Close
          </Button>
          <Button type="submit" variant="primary" disabled={busy}>
            {busy ? 'Adding…' : 'Add operator'}
          </Button>
        </div>
      </form>
    </Card>
  );
}
