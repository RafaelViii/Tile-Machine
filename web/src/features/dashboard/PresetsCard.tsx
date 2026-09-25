import { Link } from 'react-router';
import { MODULE_NAMES, syncBadge } from '../../components/ConfigBar';
import { PresetMenu } from '../../components/PresetMenu';
import { Badge, Button, Card, CardTitle } from '../../components/ui';
import { diffConfig } from '../../shared/configDiff';
import { useConfigDrafts } from '../../shared/configDrafts';
import { syncState } from '../../shared/hooks/useConfigEditor';
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
          <div className="flex flex-wrap items-center gap-x-4 gap-y-2 text-xs text-zinc-400">
            <span>No unsaved changes. Machine settings:</span>
            {MODULE_IDS.map((id) => {
              const node = modules[id].node;
              const b = d.saving ? { tone: 'zinc' as const, text: 'Saving…' } : syncBadge[syncState(node?.config, node?.configApplied)];
              return (
                <span key={id} className="inline-flex items-center gap-1.5">
                  <span className="text-zinc-300">{MODULE_NAMES[id]}</span>
                  <Badge tone={b.tone}>{b.text}</Badge>
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
                      <span className="text-[11px] text-zinc-500">offline: applies on reconnect</span>
                    )}
                  </div>
                  <ul className="space-y-1.5">
                    {rows.map((r) => (
                      <li key={r.label} className="text-xs">
                        <div className="text-zinc-400">{r.label}</div>
                        <div className="font-mono">
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
