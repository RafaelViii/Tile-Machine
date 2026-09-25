import { useConfigDrafts } from '../shared/configDrafts';
import type { SyncState } from '../shared/hooks/useConfigEditor';
import type { ModuleId } from '../shared/types/rtdb';
import { Badge, Button } from './ui';

export const syncBadge: Record<SyncState, { tone: 'green' | 'amber' | 'red' | 'zinc' | 'sky'; text: string }> = {
  never: { tone: 'zinc', text: 'Defaults (not saved yet)' },
  synced: { tone: 'green', text: 'Synced to module ✓' },
  pending: { tone: 'amber', text: 'Saved · waiting for module' },
  queued: { tone: 'sky', text: 'Module will apply when idle' },
  rejected: { tone: 'red', text: 'Module rejected this config' },
};

export const MODULE_NAMES: Record<ModuleId, string> = { shredder: 'Shredder', containing: 'Containing', hotpress: 'Hot Press' };

/** Save / reset bar with this module's sync state. Saves every module with changes (a Dashboard preset can change several). */
export function ConfigBar({
  id,
  sync,
  version,
  connected,
}: {
  id: ModuleId;
  sync: SyncState;
  version: number;
  connected: boolean;
}) {
  const { dirty, dirtyIds, saving, error, saveAll, reset } = useConfigDrafts();
  const others = dirtyIds.filter((m) => m !== id);
  const any = dirtyIds.length > 0;
  // The RTDB shows the new value locally before the server confirms, so trust the badge only after the save resolves.
  const b = saving ? { tone: 'zinc' as const, text: 'Saving…' } : syncBadge[sync];

  return (
    <div className="bar sticky bottom-3 z-20 mt-6 flex flex-wrap items-center gap-3 rounded-2xl border border-zinc-800 px-4 py-3 shadow-lg backdrop-blur">
      <div className="flex min-w-0 flex-1 flex-wrap items-center gap-x-2 gap-y-1 text-xs text-zinc-400">
        <Badge tone={b.tone}>{b.text}</Badge>
        {version > 0 && <span className="font-mono">v{version}</span>}
        {sync === 'pending' && !connected && <span>Module offline, so it applies on reconnect.</span>}
        {dirty[id] && <span className="text-amber-300">Unsaved changes</span>}
        {others.length > 0 && (
          <span className="text-amber-300">
            {dirty[id] ? 'also' : 'Unsaved'} in {others.map((m) => MODULE_NAMES[m]).join(', ')}
          </span>
        )}
        {error && <span className="text-red-400">Save failed: {error}</span>}
      </div>
      <div className="ml-auto flex gap-2">
        <Button variant="ghost" disabled={!any || saving} onClick={reset}>
          Reset
        </Button>
        <Button variant="primary" disabled={!any || saving} onClick={saveAll}>
          {saving ? 'Saving…' : dirtyIds.length > 1 ? `Save all (${dirtyIds.length})` : 'Save config'}
        </Button>
      </div>
    </div>
  );
}
