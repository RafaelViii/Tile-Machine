import { useEffect, useState } from 'react';
import { endBefore, get, limitToLast, onValue, orderByChild, query, ref, type DataSnapshot, type Query } from 'firebase/database';
import { db } from '../../lib/firebase';
import { Badge, Button, Card, EmptyNote, PageHeader, Spinner } from '../../components/ui';
import { dateTime, humanize } from '../../shared/format';
import type { EventNode } from '../../shared/types/rtdb';

// Server-side, cursor ("keyset") pagination over /events ordered by `ts` (indexed in the RTDB
// rules, so the server returns only the requested page). RTDB has no offsets: a page is
// "the N newest events older than the cursor". Each page asks for N+1 items; the extra one only
// tells us whether an older page exists. The newest page is live, older pages are fetched once.

type Row = EventNode & { id: string };
interface Cursor {
  ts: number;
  id: string;
}

const PAGE_SIZES = [25, 50, 100] as const;

function tone(code: string): 'green' | 'amber' | 'red' | 'zinc' | 'sky' {
  if (/ESTOP|JAM|FAULT|TIMEOUT|OFFLINE|NOT_ENOUGH/.test(code)) return 'red';
  if (/CANCELLED|REPLACED/.test(code)) return 'amber';
  if (/ONLINE|FINISHED|APPLIED/.test(code)) return 'green';
  if (/STARTED|BOOT/.test(code)) return 'sky';
  return 'zinc';
}

function pageQuery(size: number, before: Cursor | null): Query {
  const base = ref(db, 'events');
  return before
    ? query(base, orderByChild('ts'), endBefore(before.ts, before.id), limitToLast(size + 1))
    : query(base, orderByChild('ts'), limitToLast(size + 1));
}

/** Snapshot (ascending by ts) -> newest-first rows, plus whether an older page exists. */
function toPage(snap: DataSnapshot, size: number) {
  const asc: Row[] = [];
  snap.forEach((c) => {
    asc.push({ id: c.key!, ...(c.val() as EventNode) });
  });
  const hasOlder = asc.length > size;
  const rows = (hasOlder ? asc.slice(asc.length - size) : asc).reverse();
  return { rows, hasOlder };
}

export function EventsPage() {
  const [size, setSize] = useState<number>(50);
  // cursors[i] = where page i+2 starts (the oldest row of page i+1). Page 1 has no cursor.
  const [cursors, setCursors] = useState<Cursor[]>([]);
  const [rows, setRows] = useState<Row[] | undefined>(undefined);
  const [hasOlder, setHasOlder] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [reload, setReload] = useState(0);

  const page = cursors.length + 1;
  const cursor = cursors.length ? cursors[cursors.length - 1] : null;

  useEffect(() => {
    setRows(undefined);
    setError(null);
    const q = pageQuery(size, cursor);
    if (!cursor) {
      // Newest page: live, new events appear instantly (the SDK only transfers the changes).
      return onValue(
        q,
        (snap) => {
          const p = toPage(snap, size);
          setRows(p.rows);
          setHasOlder(p.hasOlder);
        },
        (e) => setError(e.message),
      );
    }
    // Older pages: fetched once, they don't change.
    let cancelled = false;
    get(q)
      .then((snap) => {
        if (cancelled) return;
        const p = toPage(snap, size);
        setRows(p.rows);
        setHasOlder(p.hasOlder);
      })
      .catch((e: Error) => !cancelled && setError(e.message));
    return () => {
      cancelled = true;
    };
  }, [size, cursor, reload]);

  const goOlder = () => {
    if (!rows?.length) return;
    const last = rows[rows.length - 1];
    setCursors([...cursors, { ts: last.ts, id: last.id }]);
  };
  const goNewer = () => setCursors(cursors.slice(0, -1));
  const goNewest = () => setCursors([]);
  const changeSize = (s: number) => {
    setSize(s);
    setCursors([]); // cursors belong to a page size; start over
  };

  const pager = (
    <div className="flex flex-wrap items-center justify-between gap-3">
      <div className="flex flex-wrap items-center gap-2">
        <Button variant="ghost" disabled={page === 1} onClick={goNewest}>
          « Newest
        </Button>
        <Button disabled={page === 1} onClick={goNewer}>
          ← Newer
        </Button>
        <span className="px-1 font-mono text-sm text-zinc-400">Page {page}</span>
        <Button disabled={!hasOlder || rows === undefined} onClick={goOlder}>
          Older →
        </Button>
        {page > 1 && (
          <Button variant="ghost" onClick={() => setReload((n) => n + 1)} title="Fetch this page again">
            Refresh
          </Button>
        )}
      </div>
      <label className="flex items-center gap-2 text-xs text-zinc-400">
        Per page
        <select
          value={size}
          onChange={(e) => changeSize(Number(e.target.value))}
          className="rounded-lg bg-zinc-950 px-2 py-1.5 text-sm text-zinc-100 ring-1 ring-zinc-700 outline-none focus:ring-emerald-500"
        >
          {PAGE_SIZES.map((s) => (
            <option key={s} value={s}>
              {s}
            </option>
          ))}
        </select>
      </label>
    </div>
  );

  return (
    <>
      <PageHeader
        title="Events"
        subtitle={page === 1 ? 'Newest first · this page updates live' : 'Older events · fetched once'}
        right={page === 1 ? <Badge tone="green">Live</Badge> : undefined}
      />
      <Card>
        {pager}
        <div className="mt-4">
          {error ? (
            <p className="text-sm text-red-400">{error}</p>
          ) : rows === undefined ? (
            <Spinner />
          ) : rows.length === 0 ? (
            <EmptyNote>
              {page === 1
                ? 'No events yet. They appear here when the hub starts, modules connect, runs finish or STOP is pressed.'
                : 'No older events.'}
            </EmptyNote>
          ) : (
            <div className="overflow-x-auto">
              <table className="w-full text-left text-sm">
                <thead className="text-[11px] tracking-wide text-zinc-500 uppercase">
                  <tr>
                    <th className="py-2 pr-4 font-medium">Time</th>
                    <th className="py-2 pr-4 font-medium">Source</th>
                    <th className="py-2 pr-4 font-medium">Event</th>
                    <th className="py-2 font-medium">Details</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-zinc-800">
                  {rows.map((e) => (
                    <tr key={e.id} data-event-id={e.id}>
                      <td className="py-2 pr-4 font-mono text-xs whitespace-nowrap text-zinc-400">{dateTime(e.ts)}</td>
                      <td className="py-2 pr-4 capitalize">{e.module}</td>
                      <td className="py-2 pr-4">
                        <Badge tone={tone(e.code)}>{humanize(e.code)}</Badge>
                      </td>
                      <td className="py-2 font-mono text-xs text-zinc-500">{e.args?.join(', ') ?? ''}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </div>
        {rows && rows.length > 10 && <div className="mt-4">{pager}</div>}
      </Card>
    </>
  );
}
