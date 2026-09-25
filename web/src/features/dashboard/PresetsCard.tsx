import { Link } from 'react-router';
import { MODULE_NAMES, syncBadge } from '../../components/ConfigBar';
import { PresetMenu } from '../../components/PresetMenu';
import { Button, Card, CardTitle, cx } from '../../components/ui';
import { diffConfig } from '../../shared/configDiff';
import { useConfigDrafts } from '../../shared/configDrafts';
import { syncState, type SyncState } from '../../shared/hooks/useConfigEditor';
import { useMachine } from '../../shared/machine';
import { MODULE_IDS } from '../../shared/types/rtdb';

const PAGE = { shredder: '/shredder', containing: '/containing', hotpress: '/hotpress' } as const;

/**
 * Whole-machine presets. A preset spans every module, so it lives on the Dashboard:
 * pick one, review exactly what changes, then save it to the machine in one go.
 */
export function PresetsCard() {
  const { modules } = useMachine();
  const d = useConfigDrafts();
  const changes = d.dirtyIds.map((id) => ({ id, rows: diffConfig(id, d.saved[id], d.current[id]) }));
  const total = changes.reduce((n, c) => n + c.rows.length, 0);

  return (
    <Card className="mt-6">
      <CardTitle>Machine presets</CardTitle>
      <p className="mb-4 max-w-2xl text-sm text-zinc-400">
        A preset holds every setting of the Shredder, Containing and Hot Press. Picking one only shows the changes
        below; nothing reaches the machine until you press <b className="text-zinc-300">Save to machine</b>. Each
        module applies new settings only when it is idle.
      </p>

      <PresetMenu />

      <div className="mt-5 border-t border-zinc-800 pt-4">
        {changes.length === 0 ? (
          <div className="flex flex-wrap items-center gap-x-5 gap-y-2 text-xs text-zinc-400">
            <span>No unsaved changes</span>
            {MODULE_IDS.map((id) => {
              const node = modules[id].node;
              const s = syncState(node?.config, node?.configApplied);
              const text = d.saving ? 'Saving…' : syncBadge[s].text;
              return (
                <span key={id} className="inline-flex items-center gap-1.5" title={`${MODULE_NAMES[id]}: ${text}`}>
                  <SyncIcon state={d.saving ? 'pending' : s} label={text} />
                  <span className="text-zinc-300">{MODULE_NAMES[id]}</span>
                </span>
              );
            })}
          </div>
        ) : (
          <>
            <div className="mb-3 text-sm font-medium text-amber-300">
              {total} unsaved {total === 1 ? 'change' : 'changes'} in {changes.length}{' '}
              {changes.length === 1 ? 'module' : 'modules'}
            </div>
            <div className="grid gap-3 md:grid-cols-2 lg:grid-cols-3">
              {changes.map(({ id, rows }) => (
                <div key={id} className="rounded-xl bg-zinc-950/60 p-3 ring-1 ring-zinc-800">
                  <div className="mb-2 flex items-center justify-between gap-2">
                    <Link to={PAGE[id]} className="text-sm font-semibold hover:underline">
                      {MODULE_NAMES[id]}
                    </Link>
                    {!modules[id].connected && (
                      <span className="text-xs text-zinc-500">offline: applies on reconnect</span>
                    )}
                  </div>
                  <ul className="space-y-1.5">
                    {rows.map((r) => (
                      <li key={r.label} className="text-xs">
                        <div className="text-zinc-400">{r.label}</div>
                        <div className="tabular-nums">
                          <span className="text-zinc-500 line-through">{r.from}</span>
                          <span className="px-1.5 text-zinc-600">→</span>
                          <span className="text-emerald-300">{r.to}</span>
                        </div>
                      </li>
                    ))}
                  </ul>
                </div>
              ))}
            </div>
            <div className="mt-4 flex flex-wrap items-center justify-end gap-2">
              {d.error && <span className="mr-auto text-xs text-red-400">Save failed: {d.error}</span>}
              <Button variant="ghost" disabled={d.saving} onClick={d.reset}>
                Discard changes
              </Button>
              <Button variant="primary" disabled={d.saving} onClick={d.saveAll}>
                {d.saving ? 'Saving…' : 'Save to machine'}
              </Button>
            </div>
          </>
        )}
      </div>
    </Card>
  );
}

/** Small status icon for a module's config sync: ✓ synced, clock waiting, ! rejected, dashed = never saved. */
function SyncIcon({ state, label }: { state: SyncState; label: string }) {
  const tone = {
    synced: 'text-emerald-400',
    pending: 'text-amber-400',
    queued: 'text-sky-400',
    rejected: 'text-red-400',
    never: 'text-zinc-500',
  }[state];
  return (
    <svg
      viewBox="0 0 16 16"
      className={cx('h-4 w-4 shrink-0', tone)}
      fill="none"
      stroke="currentColor"
      strokeWidth={1.6}
      strokeLinecap="round"
      strokeLinejoin="round"
      role="img"
      aria-label={label}
    >
      <circle cx="8" cy="8" r="6.5" strokeDasharray={state === 'never' ? '2.2 2' : undefined} />
      {state === 'synced' && <path d="M5 8.3l2 2 4-4.3" />}
      {(state === 'pending' || state === 'queued') && <path d="M8 4.8V8l2 1.4" />}
      {state === 'rejected' && <path d="M8 4.8v3.6M8 11h.01" />}
    </svg>
  );
}
