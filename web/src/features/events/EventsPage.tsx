import { useEffect, useState } from 'react';
import { limitToLast, onValue, orderByChild, query, ref } from 'firebase/database';
import { db } from '../../lib/firebase';
import { Badge, Card, EmptyNote, PageHeader, Spinner } from '../../components/ui';
import { dateTime, humanize } from '../../shared/format';
import type { EventNode } from '../../shared/types/rtdb';

const LIMIT = 200;

function tone(code: string): 'green' | 'amber' | 'red' | 'zinc' | 'sky' {
  if (/ESTOP|JAM|FAULT|TIMEOUT|OFFLINE|NOT_ENOUGH/.test(code)) return 'red';
  if (/CANCELLED|REPLACED/.test(code)) return 'amber';
  if (/ONLINE|FINISHED|APPLIED/.test(code)) return 'green';
  if (/STARTED|BOOT/.test(code)) return 'sky';
  return 'zinc';
}

export function EventsPage() {
  const [events, setEvents] = useState<(EventNode & { id: string })[] | undefined>(undefined);
  const [error, setError] = useState<string | null>(null);

  useEffect(
    () =>
      onValue(
        query(ref(db, 'events'), orderByChild('ts'), limitToLast(LIMIT)),
        (snap) => {
          const list: (EventNode & { id: string })[] = [];
          snap.forEach((child) => {
            list.push({ id: child.key!, ...(child.val() as EventNode) });
          });
          setEvents(list.reverse());
        },
        (e) => setError(e.message),
      ),
    [],
  );

  return (
    <>
      <PageHeader title="Events" subtitle={`Latest ${LIMIT} events from the hub and modules`} />
      <Card>
        {error ? (
          <p className="text-sm text-red-400">{error}</p>
        ) : events === undefined ? (
          <Spinner />
        ) : events.length === 0 ? (
          <EmptyNote>No events yet. They appear here when the hub starts, modules connect, runs finish or STOP is pressed.</EmptyNote>
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
                {events.map((e) => (
                  <tr key={e.id}>
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
      </Card>
    </>
  );
}
