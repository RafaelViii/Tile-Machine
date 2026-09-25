import { Fragment, useEffect, useState } from 'react';
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
import { Badge, Button, Card, EmptyNote, PageHeader, Spinner } from '../../components/ui';
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
            <ActivityList rows={rows} nameOf={nameOf} />
          )}
        </div>
        {rows && rows.length > 10 && <div className="mt-4">{pager}</div>}
      </Card>
    </>
  );
}

function resultBadge(r: Row) {
  if (!r.result) return null;
  return (
    <Badge tone={r.result === 'done' ? 'green' : 'red'}>
      {r.result === 'done' ? 'Done' : r.result === 'failed' ? 'Failed' : 'Expired'}
    </Badge>
  );
}

/** Same layout as the Events page: stacked rows on phones, a table with fixed columns from sm up. */
function ActivityList({ rows, nameOf }: { rows: Row[]; nameOf: (uid: string, email: string) => string }) {
  const [open, setOpen] = useState<Record<string, boolean>>({});
  const toggle = (id: string) => setOpen((o) => ({ ...o, [id]: !o[id] }));
  const changesButton = (r: Row) => {
    const n = r.changes?.length ?? 0;
    if (!n) return null;
    return (
      <Button variant="ghost" size="sm" onClick={() => toggle(r.id)} aria-expanded={!!open[r.id]}>
        {open[r.id] ? 'Hide' : `${n} change${n === 1 ? '' : 's'}`}
      </Button>
    );
  };
  const changes = (r: Row) =>
    open[r.id] && r.changes?.length ? (
      <ul className="grid gap-1 rounded-xl bg-zinc-950/60 p-3 text-xs ring-1 ring-zinc-800 sm:grid-cols-2">
        {r.changes.map((c, i) => (
          <li key={i}>
            <span className="text-zinc-400">{c.label}: </span>
            <span className="text-zinc-500 line-through tabular-nums">{c.from}</span>
            <span className="px-1 text-zinc-600">→</span>
            <span className="text-emerald-300 tabular-nums">{c.to}</span>
          </li>
        ))}
      </ul>
    ) : null;
  const action = (r: Row) => ACTION[r.action] ?? { label: r.action, tone: 'zinc' as const };

  return (
    <>
      {/* Phone: stacked rows (action + person, then time + result, then what happened). */}
      <ul className="divide-y divide-zinc-800 sm:hidden">
        {rows.map((r) => (
          <li key={r.id} className="py-2.5">
            <div className="flex items-center justify-between gap-2">
              <Badge tone={action(r).tone}>{action(r).label}</Badge>
              <span className="truncate text-sm">{nameOf(r.uid, r.email)}</span>
            </div>
            <div className="mt-1 flex items-center justify-between gap-3 text-xs text-zinc-500 tabular-nums">
              <span className="whitespace-nowrap">{typeof r.ts === 'number' ? dateTime(r.ts) : ''}</span>
              {resultBadge(r)}
            </div>
            <div className="mt-1 text-sm">{r.summary}</div>
            {changesButton(r) && <div className="mt-1">{changesButton(r)}</div>}
            {changes(r) && <div className="mt-2">{changes(r)}</div>}
          </li>
        ))}
      </ul>
      <div className="hidden overflow-x-auto sm:block">
        <table className="w-full text-left text-sm">
          <thead className="text-xs font-medium text-zinc-500">
            <tr>
              <th className="py-2 pr-4 font-medium">Time</th>
              <th className="py-2 pr-4 font-medium">Person</th>
              <th className="py-2 pr-4 font-medium">Action</th>
              <th className="py-2 pr-4 font-medium">Details</th>
              <th className="py-2 text-right font-medium">Result</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-zinc-800">
            {rows.map((r) => (
              <Fragment key={r.id}>
                <tr>
                  <td className="py-2 pr-4 text-xs whitespace-nowrap text-zinc-400 tabular-nums">
                    {typeof r.ts === 'number' ? dateTime(r.ts) : ''}
                  </td>
                  <td className="py-2 pr-4 whitespace-nowrap">{nameOf(r.uid, r.email)}</td>
                  <td className="py-2 pr-4">
                    <span className="whitespace-nowrap">
                      <Badge tone={action(r).tone}>{action(r).label}</Badge>
                    </span>
                  </td>
                  <td className="py-2 pr-4">{r.summary}</td>
                  <td className="py-2 text-right whitespace-nowrap">
                    <span className="inline-flex items-center gap-2">
                      {resultBadge(r)}
                      {changesButton(r)}
                    </span>
                  </td>
                </tr>
                {changes(r) && (
                  <tr className="border-t-0">
                    <td colSpan={5} className="pb-3">
                      {changes(r)}
                    </td>
                  </tr>
                )}
              </Fragment>
            ))}
          </tbody>
        </table>
      </div>
    </>
  );
}
