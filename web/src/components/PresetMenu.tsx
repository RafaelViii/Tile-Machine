import { useCallback, useRef, useState, type FormEvent, type ReactNode } from 'react';
import { normalizeConfig } from '../shared/configDefaults';
import { sameConfig, useConfigDrafts } from '../shared/configDrafts';
import { useDismiss } from '../shared/hooks/useDismiss';
import { PRESET_NAME_MAX, usePresets, type Preset } from '../shared/hooks/usePresets';
import { MODULE_IDS } from '../shared/types/rtdb';
import { CheckIcon, ChevronDown, PencilIcon, PresetIcon, TrashIcon } from './icons';
import { Button, cx } from './ui';

type Mode = { kind: 'list' } | { kind: 'new' } | { kind: 'rename'; id: string } | { kind: 'delete'; id: string };

/**
 * Whole-machine presets. Picking one fills the Shredder, Containing and Hot Press forms;
 * nothing reaches the machine until the config is saved.
 */
export function PresetMenu() {
  const drafts = useConfigDrafts();
  const { presets, loading, create, overwrite, rename, remove } = usePresets();
  const [open, setOpen] = useState(false);
  const [mode, setMode] = useState<Mode>({ kind: 'list' });
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const box = useRef<HTMLDivElement>(null);

  const close = useCallback(() => {
    setOpen(false);
    setMode({ kind: 'list' });
    setError(null);
  }, []);
  useDismiss(box, open, close);

  const matches = (p: Preset) =>
    MODULE_IDS.every((id) => !p[id] || sameConfig(drafts.current[id], normalizeConfig(id, p[id])));

  // The preset shown on the button: the one picked here, or else one that matches the current setup.
  const active = presets.find((p) => p.id === drafts.presetId) ?? presets.find(matches) ?? null;
  const edited = !!active && !matches(active);

  const run = async (fn: () => Promise<unknown>, after: Mode = { kind: 'list' }) => {
    setBusy(true);
    setError(null);
    try {
      await fn();
      setMode(after);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  const pick = (p: Preset) => {
    drafts.load({ shredder: p.shredder, containing: p.containing, hotpress: p.hotpress });
    drafts.setPresetId(p.id);
    close();
  };

  const nameTaken = (name: string, except?: string) =>
    presets.some((p) => p.id !== except && p.name.trim().toLowerCase() === name.trim().toLowerCase());

  return (
    <div ref={box} className="relative">
      <button
        type="button"
        aria-haspopup="menu"
        aria-expanded={open}
        onClick={() => (open ? close() : setOpen(true))}
        className="inline-flex max-w-[16rem] items-center gap-2 rounded-lg bg-zinc-950 px-3 py-2 text-sm ring-1 ring-zinc-700 transition hover:ring-zinc-500"
      >
        <PresetIcon className="h-4 w-4 shrink-0 text-zinc-400" />
        <span className="truncate">
          {active ? active.name : <span className="text-zinc-400">No preset</span>}
        </span>
        {edited && (
          <span className="shrink-0 rounded bg-amber-500/15 px-1.5 text-[10px] font-semibold text-amber-300 uppercase">
            edited
          </span>
        )}
        <ChevronDown className={cx('h-4 w-4 shrink-0 text-zinc-500 transition', open && 'rotate-180')} />
      </button>

      {open && (
        <div
          role="menu"
          className="popover absolute bottom-full left-0 z-40 mb-2 w-[min(20rem,calc(100vw-2rem))] rounded-xl border p-2"
        >
          <div className="px-2 pt-1 pb-2">
            <div className="text-xs font-semibold tracking-wide text-zinc-300 uppercase">Machine presets</div>
            <p className="mt-0.5 text-[11px] leading-snug text-zinc-500">
              A preset holds Shredder, Containing and Hot Press settings. Picking one fills the forms; nothing
              is sent until you save.
            </p>
          </div>

          <ul className="max-h-64 overflow-y-auto">
            {loading && <li className="px-2 py-2 text-sm text-zinc-500">Loading…</li>}
            {!loading && presets.length === 0 && (
              <li className="px-2 py-2 text-sm text-zinc-500">No presets yet.</li>
            )}
            {presets.map((p) =>
              mode.kind === 'rename' && mode.id === p.id ? (
                <li key={p.id} className="px-1 py-1">
                  <NameForm
                    initial={p.name}
                    submitLabel="Rename"
                    busy={busy}
                    taken={(n) => nameTaken(n, p.id)}
                    onCancel={() => setMode({ kind: 'list' })}
                    onSubmit={(n) => run(() => rename(p.id, n))}
                  />
                </li>
              ) : mode.kind === 'delete' && mode.id === p.id ? (
                <li key={p.id} className="flex items-center gap-2 rounded-lg bg-red-500/10 px-2 py-1.5">
                  <span className="min-w-0 flex-1 truncate text-sm text-red-300">Delete “{p.name}”?</span>
                  <Button variant="ghost" className="px-2 py-1 text-xs" onClick={() => setMode({ kind: 'list' })}>
                    Cancel
                  </Button>
                  <Button
                    variant="danger"
                    className="px-2 py-1 text-xs"
                    disabled={busy}
                    onClick={() =>
                      run(async () => {
                        await remove(p.id);
                        if (drafts.presetId === p.id) drafts.setPresetId(null);
                      })
                    }
                  >
                    Delete
                  </Button>
                </li>
              ) : (
                <li key={p.id} className="group flex items-center rounded-lg hover:bg-zinc-800/70">
                  <button
                    type="button"
                    role="menuitem"
                    onClick={() => pick(p)}
                    className="flex min-w-0 flex-1 items-center gap-2 px-2 py-1.5 text-left text-sm"
                  >
                    <CheckIcon
                      className={cx('h-4 w-4 shrink-0', active?.id === p.id ? 'text-emerald-400' : 'invisible')}
                    />
                    <span className="truncate">{p.name}</span>
                  </button>
                  <IconBtn label={`Rename ${p.name}`} onClick={() => setMode({ kind: 'rename', id: p.id })}>
                    <PencilIcon className="h-3.5 w-3.5" />
                  </IconBtn>
                  <IconBtn label={`Delete ${p.name}`} danger onClick={() => setMode({ kind: 'delete', id: p.id })}>
                    <TrashIcon className="h-3.5 w-3.5" />
                  </IconBtn>
                </li>
              ),
            )}
          </ul>

          <div className="mt-2 space-y-1 border-t border-zinc-800 pt-2">
            {active && edited && mode.kind === 'list' && (
              <button
                type="button"
                disabled={busy}
                onClick={() => run(() => overwrite(active.id, drafts.current))}
                className="w-full truncate rounded-lg px-2 py-1.5 text-left text-sm text-emerald-300 hover:bg-zinc-800/70 disabled:opacity-40"
              >
                Update “{active.name}” with current settings
              </button>
            )}
            {mode.kind === 'new' ? (
              <div className="px-1 py-1">
                <NameForm
                  initial=""
                  submitLabel="Save"
                  busy={busy}
                  taken={(n) => nameTaken(n)}
                  onCancel={() => setMode({ kind: 'list' })}
                  onSubmit={(n) =>
                    run(async () => {
                      const id = await create(n, drafts.current);
                      drafts.setPresetId(id);
                    })
                  }
                />
              </div>
            ) : (
              <button
                type="button"
                onClick={() => setMode({ kind: 'new' })}
                className="w-full rounded-lg px-2 py-1.5 text-left text-sm text-zinc-300 hover:bg-zinc-800/70"
              >
                + Save current settings as new preset
              </button>
            )}
          </div>

          {error && <p className="px-2 pt-2 text-xs text-red-400">{error}</p>}
        </div>
      )}
    </div>
  );
}

function IconBtn({
  label,
  danger,
  onClick,
  children,
}: {
  label: string;
  danger?: boolean;
  onClick: () => void;
  children: ReactNode;
}) {
  return (
    <button
      type="button"
      title={label}
      aria-label={label}
      onClick={onClick}
      className={cx(
        'rounded-md p-1.5 text-zinc-500 opacity-60 transition group-hover:opacity-100 focus-visible:opacity-100',
        danger ? 'hover:text-red-400' : 'hover:text-zinc-100',
      )}
    >
      {children}
    </button>
  );
}

function NameForm({
  initial,
  submitLabel,
  busy,
  taken,
  onSubmit,
  onCancel,
}: {
  initial: string;
  submitLabel: string;
  busy: boolean;
  taken: (name: string) => boolean;
  onSubmit: (name: string) => void;
  onCancel: () => void;
}) {
  const [name, setName] = useState(initial);
  const trimmed = name.trim();
  const dup = trimmed !== '' && taken(trimmed);
  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (trimmed && !dup) onSubmit(trimmed);
  };
  return (
    <form onSubmit={submit}>
      <div className="flex items-center gap-1.5">
        <input
          autoFocus
          value={name}
          maxLength={PRESET_NAME_MAX}
          placeholder="Preset name"
          onChange={(e) => setName(e.target.value)}
          className="min-w-0 flex-1 rounded-lg bg-zinc-950 px-2 py-1.5 text-sm text-zinc-100 ring-1 ring-zinc-700 outline-none focus:ring-emerald-500"
        />
        <Button type="button" variant="ghost" className="px-2 py-1 text-xs" onClick={onCancel}>
          Cancel
        </Button>
        <Button type="submit" variant="primary" className="px-2.5 py-1 text-xs" disabled={busy || !trimmed || dup}>
          {submitLabel}
        </Button>
      </div>
      {dup && <p className="mt-1 text-[11px] text-amber-300">A preset with this name already exists.</p>}
    </form>
  );
}
