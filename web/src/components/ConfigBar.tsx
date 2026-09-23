import type { SyncState } from '../shared/hooks/useConfigEditor';
import { Badge, Button } from './ui';

const syncBadge: Record<SyncState, { tone: 'green' | 'amber' | 'red' | 'zinc' | 'sky'; text: string }> = {
  never: { tone: 'zinc', text: 'Defaults (not saved yet)' },
  synced: { tone: 'green', text: 'Synced to module ✓' },
  pending: { tone: 'amber', text: 'Saved · waiting for module' },
  queued: { tone: 'sky', text: 'Module will apply when idle' },
  rejected: { tone: 'red', text: 'Module rejected this config' },
};

/** Save / reset bar with the module sync state. */
export function ConfigBar({
  dirty,
  saving,
  error,
  sync,
  version,
  connected,
  onSave,
  onReset,
}: {
  dirty: boolean;
  saving: boolean;
  error: string | null;
  sync: SyncState;
  version: number;
  connected: boolean;
  onSave: () => void;
  onReset: () => void;
}) {
  // The RTDB shows the new value locally before the server confirms, so trust the badge only after the save resolves.
  const b = saving ? { tone: 'zinc' as const, text: 'Saving…' } : syncBadge[sync];
  return (
    <div className="sticky bottom-3 z-20 mt-6 flex flex-wrap items-center justify-between gap-3 rounded-2xl border border-zinc-800 bg-zinc-900/95 px-4 py-3 shadow-2xl backdrop-blur">
      <div className="flex flex-wrap items-center gap-2 text-xs text-zinc-400">
        <Badge tone={b.tone}>{b.text}</Badge>
        {version > 0 && <span className="font-mono">v{version}</span>}
        {sync === 'pending' && !connected && <span>Module offline, so it applies on reconnect.</span>}
        {dirty && <span className="text-amber-300">Unsaved changes</span>}
        {error && <span className="text-red-400">Save failed: {error}</span>}
      </div>
      <div className="flex gap-2">
        <Button variant="ghost" disabled={!dirty || saving} onClick={onReset}>
          Reset
        </Button>
        <Button variant="primary" disabled={!dirty || saving} onClick={onSave}>
          {saving ? 'Saving…' : 'Save config'}
        </Button>
      </div>
    </div>
  );
}
