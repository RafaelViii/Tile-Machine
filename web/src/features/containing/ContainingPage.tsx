import { useState } from 'react';
import { CommandButton } from '../../components/CommandButton';
import { ConfigBar } from '../../components/ConfigBar';
import { ModulePanel } from '../../components/ModulePanel';
import { NumberField } from '../../components/NumberField';
import { SlideToggle } from '../../components/SlideToggle';
import { Badge, Button, Card, CardTitle, Stat, cx } from '../../components/ui';
import { LIMITS } from '../../shared/configDefaults';
import { humanize, kg, secs } from '../../shared/format';
import { useConfigEditor } from '../../shared/hooks/useConfigEditor';
import { useMachine } from '../../shared/machine';
import type { ContainerState, ContainingConfig } from '../../shared/types/rtdb';

const L = LIMITS.containing;
const NAMES = ['Raw HDPE', 'Raw PP', 'Mixed HDPE', 'Mixed PP'];
const CONTROLS = [
  'SELECT 1 + CONFIRM 1',
  'SELECT 2 + CONFIRM 2',
  'Selector LEFT',
  'Selector RIGHT',
];

type Update = (fn: (c: ContainingConfig) => ContainingConfig) => void;

function stateTone(state?: string): 'green' | 'amber' | 'red' | 'zinc' | 'sky' {
  if (!state) return 'zinc';
  if (state === 'DISPENSING') return 'green';
  if (state === 'FAULT' || state === 'NOT_ENOUGH') return 'red';
  if (state === 'DONE') return 'sky';
  if (state === 'CANCELLED') return 'amber';
  return 'zinc';
}

function LiveRow({ c, hxOk, isRaw }: { c?: ContainerState; hxOk: boolean; isRaw: boolean }) {
  if (!c) return <p className="text-xs text-zinc-500">Live data appears once the module connects.</p>;
  return (
    <div>
      <div className="grid grid-cols-3 gap-2">
        <Stat label="Weight" value={hxOk ? kg(c.weightG) : 'Sensor fault'} tone={hxOk ? undefined : 'red'} />
        {isRaw ? (
          <Stat label="Selected" value={`${c.selectedKg} kg`} tone="sky" />
        ) : (
          <Stat label="Remaining" value={c.remainingMs ? secs(c.remainingMs) : '—'} />
        )}
        <Stat label="This cycle" value={kg(c.dispensedG)} />
      </div>
      <div className="mt-3 flex items-center gap-3">
        <Badge tone={stateTone(c.state)}>{humanize(c.state)}</Badge>
        <div className="h-2 flex-1 overflow-hidden rounded-full bg-zinc-800">
          <div
            className={cx('h-full rounded-full transition-all', c.state === 'DISPENSING' ? 'bg-emerald-400' : 'bg-zinc-600')}
            style={{ width: `${Math.min(100, Math.max(0, c.progressPct))}%` }}
          />
        </div>
        <span className="w-10 text-right text-xs text-zinc-400 tabular-nums">{c.progressPct}%</span>
      </div>
    </div>
  );
}

function Calibration({ index, connected }: { index: number; connected: boolean }) {
  const [grams, setGrams] = useState(1000);
  const reason = 'Module not connected';
  return (
    <div className="mt-4 rounded-xl bg-zinc-950/60 p-3 ring-1 ring-zinc-800">
      <div className="mb-2 text-xs text-zinc-500">Load cell</div>
      <div className="flex flex-wrap items-start gap-3">
        <CommandButton moduleId="containing" type="TARE" target={index} disabled={!connected} disabledReason={reason}>
          Tare (zero)
        </CommandButton>
        <div className="flex items-start gap-2">
          <input
            type="number"
            min={100}
            max={50000}
            value={grams}
            onChange={(e) => setGrams(Math.max(100, Math.min(50000, Number(e.target.value) || 0)))}
            className="w-24 rounded-lg bg-zinc-950 px-2 py-2 text-sm tabular-nums ring-1 ring-zinc-700 outline-none focus:ring-emerald-500"
            aria-label="Known weight in grams"
          />
          <CommandButton
            moduleId="containing"
            type="CALIBRATE"
            target={index}
            arg={grams}
            disabled={!connected}
            disabledReason={reason}
          >
            Calibrate with {grams} g
          </CommandButton>
        </div>
      </div>
      <p className="mt-2 text-xs text-zinc-500">Empty the container → Tare. Put a known weight in → Calibrate.</p>
    </div>
  );
}

function RawCard({ i, cfg, update, live, hxOk, connected }: {
  i: 0 | 1;
  cfg: ContainingConfig;
  update: Update;
  live?: ContainerState;
  hxOk: boolean;
  connected: boolean;
}) {
  const r = cfg.raw[i];
  const set = (patch: Partial<typeof r>) =>
    update((c) => {
      c.raw[i] = { ...c.raw[i], ...patch };
      return c;
    });

  return (
    <Card>
      <CardTitle right={<span className="text-xs text-zinc-500">{CONTROLS[i]}</span>}>
        C{i + 1} · {NAMES[i]}
      </CardTitle>
      <LiveRow c={live} hxOk={hxOk} isRaw />

      <div className="mt-5 flex flex-wrap items-center justify-between gap-2">
        <span className="text-xs font-medium text-zinc-400">Dispense mode</span>
        <SlideToggle
          left={{ value: 'LOADCELL', label: 'Loadcell' }}
          right={{ value: 'TIME', label: 'Time' }}
          value={r.mode}
          onChange={(mode) => set({ mode })}
        />
      </div>

      {r.mode === 'TIME' ? (
        <div className="mt-4">
          <div className="grid grid-cols-5 gap-2">
            {r.timeTableMs.map((t, k) => (
              <NumberField
                key={k}
                label={`${k + 1} kg`}
                value={t}
                onChange={(v) => set({ timeTableMs: r.timeTableMs.map((x, j) => (j === k ? v : x)) })}
                min={L.timeTableMs[0]}
                max={L.timeTableMs[1]}
                scale={1000}
                step={0.5}
                unit="s"
              />
            ))}
          </div>
          <div className="mt-2 flex flex-wrap items-center justify-between gap-2">
            <span className="text-xs text-zinc-500">How long the screws run for each amount.</span>
            <Button
              variant="ghost"
              size="sm"
              onClick={() => set({ timeTableMs: r.timeTableMs.map((_, k) => Math.min(L.timeTableMs[1], r.timeTableMs[0] * (k + 1))) })}
            >
              Fill linearly from 1 kg
            </Button>
          </div>
        </div>
      ) : (
        <div className="mt-4 grid grid-cols-3 gap-2">
          <NumberField
            label="Safety max time"
            value={r.maxDispenseMs}
            onChange={(v) => set({ maxDispenseMs: v })}
            min={L.maxDispenseMs[0]}
            max={L.maxDispenseMs[1]}
            scale={1000}
            unit="s"
          />
          <NumberField
            label="Jam if no drop for"
            value={r.jamTimeoutMs}
            onChange={(v) => set({ jamTimeoutMs: v })}
            min={L.jamTimeoutMs[0]}
            max={L.jamTimeoutMs[1]}
            scale={1000}
            unit="s"
          />
          <NumberField
            label="Tolerance"
            value={r.toleranceG}
            onChange={(v) => set({ toleranceG: v })}
            min={L.toleranceG[0]}
            max={L.toleranceG[1]}
            unit="g"
          />
        </div>
      )}

      <Calibration index={i} connected={connected} />
    </Card>
  );
}

function MixedCard({ i, cfg, update, live, hxOk, connected }: {
  i: 0 | 1;
  cfg: ContainingConfig;
  update: Update;
  live?: ContainerState;
  hxOk: boolean;
  connected: boolean;
}) {
  const idx = i + 2;
  const m = cfg.mixed[i];
  const set = (patch: Partial<typeof m>) =>
    update((c) => {
      c.mixed[i] = { ...c.mixed[i], ...patch };
      return c;
    });

  return (
    <Card>
      <CardTitle right={<span className="text-xs text-zinc-500">{CONTROLS[idx]}</span>}>
        C{idx + 1} · {NAMES[idx]}
      </CardTitle>
      <LiveRow c={live} hxOk={hxOk} isRaw={false} />

      <div className="mt-5 flex flex-wrap items-center justify-between gap-2">
        <span className="text-xs font-medium text-zinc-400">Stop mode</span>
        <SlideToggle
          left={{ value: 'MANUAL', label: 'Manual' }}
          right={{ value: 'TIME', label: 'Time' }}
          value={m.mode}
          onChange={(mode) => set({ mode })}
        />
      </div>

      {m.mode === 'TIME' ? (
        <div className="mt-4 max-w-48">
          <NumberField
            label="Run time"
            hint="Stops automatically"
            value={m.runTimeMs}
            onChange={(v) => set({ runTimeMs: v })}
            min={L.runTimeMs[0]}
            max={L.runTimeMs[1]}
            scale={1000}
            step={0.5}
            unit="s"
          />
          <p className="mt-2 text-xs text-zinc-500">To run again: move the selector back to the middle, then to this side.</p>
        </div>
      ) : (
        <p className="mt-4 text-xs text-zinc-400">Runs while the selector is on this side and stops at the middle position.</p>
      )}

      <Calibration index={idx} connected={connected} />
    </Card>
  );
}

function Advanced({ cfg, update, calFactor }: { cfg: ContainingConfig; update: Update; calFactor?: number[] }) {
  const [open, setOpen] = useState(false);
  return (
    <Card className="mt-6">
      <button type="button" onClick={() => setOpen(!open)} className="flex w-full items-center justify-between text-left">
        <span className="text-base font-semibold text-zinc-100">Advanced · servos & load cells</span>
        <span className="text-xs text-zinc-500">{open ? 'Hide' : 'Show'}</span>
      </button>
      {open && (
        <div className="mt-4">
          <p className="mb-3 text-xs text-zinc-400">
            MG995 360° servos: <b>stop pulse</b> is where the servo stands still (about 1500 µs, trim it until it stops fully).{' '}
            <b>Run pulse</b> sets speed and direction. Below the stop pulse turns one way, above turns the other. Mirrored servos
            need opposite values.
          </p>
          <div className="grid gap-3 sm:grid-cols-2 lg:grid-cols-4">
            {cfg.servo.map((s, ch) => (
              <div key={ch} className="rounded-xl bg-zinc-950/60 p-3 ring-1 ring-zinc-800">
                <div className="mb-2 text-xs text-zinc-400">
                  ch {ch} · {NAMES[Math.floor(ch / 2)]} {ch % 2 ? 'B' : 'A'}
                </div>
                <div className="grid grid-cols-2 gap-2">
                  <NumberField
                    label="Stop"
                    value={s.stopUs}
                    onChange={(v) => update((c) => ((c.servo[ch] = { ...c.servo[ch], stopUs: v }), c))}
                    min={L.stopUs[0]}
                    max={L.stopUs[1]}
                    unit="µs"
                  />
                  <NumberField
                    label="Run"
                    value={s.runUs}
                    onChange={(v) => update((c) => ((c.servo[ch] = { ...c.servo[ch], runUs: v }), c))}
                    min={L.runUs[0]}
                    max={L.runUs[1]}
                    unit="µs"
                  />
                </div>
              </div>
            ))}
          </div>
          <div className="mt-5 grid grid-cols-2 gap-3 sm:grid-cols-4">
            {[0, 1, 2, 3].map((i) => (
              <Stat
                key={i}
                label={`Cal factor C${i + 1}`}
                value={typeof calFactor?.[i] === 'number' ? calFactor[i].toFixed(4) : '—'}
              />
            ))}
          </div>
          <p className="mt-2 text-xs text-zinc-500">
            Stored on the Containing board and set only by the Calibrate buttons above, so saving config never changes them.
          </p>
        </div>
      )}
    </Card>
  );
}

export function ContainingPage() {
  const { modules } = useMachine();
  const { node, connected } = modules.containing;
  const s = connected ? node?.state : undefined;
  const cfg = useConfigEditor('containing', node?.config ?? undefined, node?.configApplied);
  const d = cfg.draft;
  const hxOk = (i: number) => (s ? ((s.hxOkMask >> i) & 1) === 1 : true);

  return (
    <ModulePanel id="containing" title="Containing" subtitle="4 containers · load cells · screw dispensers" esp="esp2">
      {s && (
        <div className="mb-6 flex flex-wrap gap-2">
          <Badge tone="sky">Selector: {s.selector === 'LEFT' ? 'LEFT (Mixed HDPE)' : s.selector === 'RIGHT' ? 'RIGHT (Mixed PP)' : 'MIDDLE (stop)'}</Badge>
          <Badge tone={s.pcaOk ? 'green' : 'red'}>Servo driver {s.pcaOk ? 'OK' : 'NOT FOUND'}</Badge>
          <Badge tone={s.hxOkMask === 15 ? 'green' : 'red'}>Load cells {s.hxOkMask === 15 ? 'OK' : 'fault'}</Badge>
        </div>
      )}
      <div className="grid gap-6 lg:grid-cols-2">
        <RawCard i={0} cfg={d} update={cfg.update} live={s?.containers?.[0]} hxOk={hxOk(0)} connected={connected} />
        <RawCard i={1} cfg={d} update={cfg.update} live={s?.containers?.[1]} hxOk={hxOk(1)} connected={connected} />
        <MixedCard i={0} cfg={d} update={cfg.update} live={s?.containers?.[2]} hxOk={hxOk(2)} connected={connected} />
        <MixedCard i={1} cfg={d} update={cfg.update} live={s?.containers?.[3]} hxOk={hxOk(3)} connected={connected} />
      </div>
      <Advanced cfg={d} update={cfg.update} calFactor={s?.calFactor} />
      <ConfigBar id="containing" sync={cfg.sync} version={cfg.version} connected={connected} />
    </ModulePanel>
  );
}
