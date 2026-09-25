import { useEffect, useState } from 'react';
import { useSearchParams } from 'react-router';
import {
  endAt,
  endBefore,
  equalTo,
  get,
  limitToFirst,
  limitToLast,
  onValue,
  orderByChild,
  orderByKey,
  query,
  ref,
  startAt,
  update,
} from 'firebase/database';
import { Badge, Button, Card, EmptyNote, PageHeader, Spinner, cx } from '../../components/ui';
import { db } from '../../lib/firebase';
import { dateTime } from '../../shared/format';
import type { AuditAction, AuditNode } from '../../shared/types/rtdb';
import { usePeople } from '../users/UsersPage';

const PAGE = 50;
const KEEP_MS = 90 * 24 * 3600 * 1000;

type Row = AuditNode & { id: string };

const ACTION: Record<AuditAction, { label: string; tone: 'green' | 'amber' | 'red' | 'zinc' | 'sky' }> = {
  SIGN_IN: { label: 'Signed in', tone: 'green' },
  SIGN_OUT: { label: 'Signed out', tone: 'zinc' },
  CONFIG_SAVE: { label: 'Settings', tone: 'amber' },
  COMMAND: { label: 'Command', tone: 'sky' },
  PRESET_CREATE: { label: 'Preset', tone: 'zinc' },
  PRESET_UPDATE: { label: 'Preset', tone: 'zinc' },
  PRESET_RENAME: { label: 'Preset', tone: 'zinc' },
  PRESET_DELETE: { label: 'Preset', tone: 'zinc' },
  USER_ADD: { label: 'Users', tone: 'zinc' },
  USER_RENAME: { label: 'Users', tone: 'zinc' },
  USER_ACCESS: { label: 'Users', tone: 'zinc' },
  PASSWORD_CHANGE: { label: 'Password', tone: 'zinc' },
  USER_PASSWORD: { label: 'Password', tone: 'amber' },
};

function toRows(val: Record<string, AuditNode> | null): Row[] {
  return Object.entries(val ?? {})
    .map(([id, v]) => ({ ...v, id }))
    .sort((a, b) => (a.id < b.id ? 1 : -1)); // push ids sort by time: newest first
}

export function ActivityPage() {
  const { people } = usePeople();
  const [params, setParams] = useSearchParams();
  const who = params.get('uid') ?? '';
  const [cursors, setCursors] = useState<string[]>([]); // oldest id of each page shown before this one
  const [rows, setRows] = useState<Row[] | undefined>(undefined);
  const [hasOlder, setHasOlder] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const page = cursors.length + 1;
  const before = cursors[cursors.length - 1];

  // Keyset pagination on push ids (= time order). Page 1 is live; older pages are fetched once.
  useEffect(() => {
    setRows(undefined);
    setError(null);
    const base = ref(db, 'audit');
    const q = who
      ? before
        ? query(base, orderByChild('uid'), startAt(who), endBefore(who, before), limitToLast(PAGE + 1))
        : query(base, orderByChild('uid'), equalTo(who), limitToLast(PAGE + 1))
      : before
        ? query(base, orderByKey(), endBefore(before), limitToLast(PAGE + 1))
        : query(base, orderByKey(), limitToLast(PAGE + 1));
    const apply = (val: Record<string, AuditNode> | null) => {
      const all = toRows(val);
      setHasOlder(all.length > PAGE);
      setRows(all.slice(0, PAGE));
    };
    const fail = (e: Error) => setError(e.message);
    if (!before) return onValue(q, (s) => apply(s.val()), fail);
    let live = true;
    get(q).then((s) => live && apply(s.val()), fail);
    return () => {
      live = false;
    };
  }, [who, before]);

  // Housekeeping: entries older than 90 days are deleted (only the superadmin may delete).
  useEffect(() => {
    const old = query(ref(db, 'audit'), orderByChild('ts'), endAt(Date.now() - KEEP_MS), limitToFirst(200));
    get(old)
      .then((s) => {
        const upd: Record<string, null> = {};
        s.forEach((c) => {
          upd[`audit/${c.key}`] = null;
        });
        if (Object.keys(upd).length) return update(ref(db), upd);
      })
      .catch(() => {});
  }, []);

  const nameOf = (uid: string, email: string) => people?.find((p) => p.uid === uid)?.name ?? email;
  const online = people?.filter((p) => p.online) ?? [];

  const setWho = (uid: string) => {
    setCursors([]);
    setParams(uid ? { uid } : {});
  };

  const pager = (
    <div className="flex flex-wrap items-center gap-2">
      <Button variant="ghost" disabled={page === 1} onClick={() => setCursors([])}>
        « Newest
      </Button>
      <Button disabled={page === 1} onClick={() => setCursors(cursors.slice(0, -1))}>
        ← Newer
      </Button>
      <span className="px-1 text-sm text-zinc-400 tabular-nums">Page {page}</span>
      <Button disabled={!hasOlder || !rows?.length} onClick={() => rows && setCursors([...cursors, rows[rows.length - 1].id])}>
        Older →
      </Button>
    </div>
  );

  return (
    <>
      <PageHeader title="Activity" subtitle="What everyone did in Tile Console, newest first. Kept for 90 days." />

      <Card className="mb-6">
        <div className="flex flex-wrap items-center justify-between gap-3">
          <div className="text-sm">
            <span className="font-semibold">Online now: </span>
            {people === undefined ? (
              <span className="text-zinc-500">…</span>
            ) : online.length === 0 ? (
              <span className="text-zinc-500">nobody</span>
            ) : (
              online.map((p, i) => (
                <span key={p.uid}>
                  {i > 0 && ', '}
                  <span className="text-emerald-300">{p.name}</span>
                  <span className="text-zinc-500"> ({p.where})</span>
                </span>
              ))
            )}
          </div>
          <label className="flex items-center gap-2 text-xs text-zinc-400">
            Person
            <select
              value={who}
              onChange={(e) => setWho(e.target.value)}
              className="rounded-lg bg-zinc-950 px-2 py-1.5 text-sm text-zinc-100 ring-1 ring-zinc-700 outline-none focus:ring-emerald-500"
            >
              <option value="">Everyone</option>
              {people?.map((p) => (
                <option key={p.uid} value={p.uid}>
                  {p.name}
                </option>
              ))}
            </select>
          </label>
        </div>
      </Card>

      <Card>
        {pager}
        <div className="mt-4">
          {error ? (
            <p className="text-sm text-red-400">{error}</p>
          ) : rows === undefined ? (
            <Spinner />
          ) : rows.length === 0 ? (
            <EmptyNote>{page === 1 ? 'Nothing recorded yet.' : 'No older activity.'}</EmptyNote>
          ) : (
            <ul className="divide-y divide-zinc-800">
              {rows.map((r) => (
                <ActivityRow key={r.id} r={r} who={nameOf(r.uid, r.email)} />
              ))}
            </ul>
          )}
        </div>
        {rows && rows.length > 10 && <div className="mt-4">{pager}</div>}
      </Card>
    </>
  );
}

function ActivityRow({ r, who }: { r: Row; who: string }) {
  const [open, setOpen] = useState(false);
  const a = ACTION[r.action] ?? { label: r.action, tone: 'zinc' as const };
  const n = r.changes?.length ?? 0;
  return (
    <li className="py-2.5">
      <div className="flex flex-wrap items-center gap-x-3 gap-y-1">
        <span className="w-40 shrink-0 text-xs text-zinc-500 tabular-nums">{typeof r.ts === 'number' ? dateTime(r.ts) : ''}</span>
        <span className="w-28 shrink-0 truncate text-sm font-medium">{who}</span>
        <Badge tone={a.tone}>{a.label}</Badge>
        <span className="min-w-0 flex-1 text-sm">{r.summary}</span>
        {r.result && (
          <Badge tone={r.result === 'done' ? 'green' : 'red'}>{r.result === 'done' ? 'Done' : r.result === 'failed' ? 'Failed' : 'Expired'}</Badge>
        )}
        {n > 0 && (
          <button
            type="button"
            onClick={() => setOpen(!open)}
            className="rounded-md px-2 py-0.5 text-xs text-zinc-400 hover:bg-zinc-800 hover:text-zinc-200"
          >
            {open ? 'Hide' : `${n} change${n === 1 ? '' : 's'}`}
          </button>
        )}
      </div>
      {open && n > 0 && (
        <ul className={cx('mt-2 grid gap-1 rounded-xl bg-zinc-950/60 p-3 text-xs ring-1 ring-zinc-800 sm:grid-cols-2')}>
          {r.changes!.map((c, i) => (
            <li key={i}>
              <span className="text-zinc-400">{c.label}: </span>
              <span className="text-zinc-500 line-through tabular-nums">{c.from}</span>
              <span className="px-1 text-zinc-600">→</span>
              <span className="text-emerald-300 tabular-nums">{c.to}</span>
            </li>
          ))}
        </ul>
      )}
    </li>
  );
}
