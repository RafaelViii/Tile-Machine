import { ConfigBar } from '../../components/ConfigBar';
import { ModulePanel } from '../../components/ModulePanel';
import { NumberField } from '../../components/NumberField';
import { Card, CardTitle, EmptyNote, Stat } from '../../components/ui';
import { LIMITS } from '../../shared/configDefaults';
import { humanize, secs } from '../../shared/format';
import { useConfigEditor } from '../../shared/hooks/useConfigEditor';
import { useMachine } from '../../shared/machine';

const L = LIMITS.shredder;

export function ShredderPage() {
  const { modules } = useMachine();
  const { node, connected } = modules.shredder;
  const s = node?.state;
  const cfg = useConfigEditor('shredder', node?.config ?? undefined, node?.configApplied);
  const d = cfg.draft;

  return (
    <ModulePanel id="shredder" title="Shredder" subtitle="3-way switch: AUTO · OFF · MANUAL" esp="esp1">
      <Card className="mb-6">
        <CardTitle>Live status</CardTitle>
        {connected && s ? (
          <div className="grid grid-cols-2 gap-3 sm:grid-cols-3 lg:grid-cols-6">
            <Stat label="Switch" value={s.mode} tone="sky" />
            <Stat label="State" value={humanize(s.state)} />
            <Stat label="Motor relay" value={s.relayOn ? 'RUNNING' : 'Off'} tone={s.relayOn ? 'green' : undefined} />
            <Stat label="IR sensor" value={s.irDetected ? 'Material' : 'Empty'} tone={s.irDetected ? 'amber' : undefined} />
            <Stat label="Countdown" value={s.countdownMs ? secs(s.countdownMs) : '—'} />
            <Stat label="E-stop" value={s.estopLatched ? 'LATCHED' : 'Clear'} tone={s.estopLatched ? 'red' : undefined} />
          </div>
        ) : (
          <EmptyNote>Live status appears here once the shredder connects.</EmptyNote>
        )}
      </Card>

      <Card>
        <CardTitle>Rules & timing</CardTitle>
        <div className="grid gap-5 sm:grid-cols-2">
          <NumberField
            label="AUTO: warning before start"
            hint="Beeps and counts down after material is detected"
            value={d.autoStartDelayMs}
            onChange={(v) => cfg.update((c) => ({ ...c, autoStartDelayMs: v }))}
            min={L.autoStartDelayMs[0]}
            max={L.autoStartDelayMs[1]}
            scale={1000}
            step={0.5}
            unit="s"
          />
          <NumberField
            label="AUTO: empty time before stop"
            hint="Chute must read empty this long before the motor stops"
            value={d.autoEmptyStopDelayMs}
            onChange={(v) => cfg.update((c) => ({ ...c, autoEmptyStopDelayMs: v }))}
            min={L.autoEmptyStopDelayMs[0]}
            max={L.autoEmptyStopDelayMs[1]}
            scale={1000}
            step={0.5}
            unit="s"
          />
          <NumberField
            label="MANUAL: time to press START again"
            hint="After the empty/loaded check, returns to idle if not confirmed"
            value={d.manualConfirmTimeoutMs}
            onChange={(v) => cfg.update((c) => ({ ...c, manualConfirmTimeoutMs: v }))}
            min={L.manualConfirmTimeoutMs[0]}
            max={L.manualConfirmTimeoutMs[1]}
            scale={1000}
            unit="s"
          />
          <NumberField
            label="IR sensor debounce"
            hint="Ignores flicker shorter than this"
            value={d.irDebounceMs}
            onChange={(v) => cfg.update((c) => ({ ...c, irDebounceMs: v }))}
            min={L.irDebounceMs[0]}
            max={L.irDebounceMs[1]}
            unit="ms"
          />
          <NumberField
            label="3-way switch debounce"
            hint="A new switch position only counts after it stays steady this long. Raise it if the screen jumps between modes"
            value={d.switchDebounceMs}
            onChange={(v) => cfg.update((c) => ({ ...c, switchDebounceMs: v }))}
            min={L.switchDebounceMs[0]}
            max={L.switchDebounceMs[1]}
            step={10}
            unit="ms"
          />
          <NumberField
            label="START / STOP button debounce"
            hint="Filters button chatter. Kept short so STOP always reacts fast"
            value={d.buttonDebounceMs}
            onChange={(v) => cfg.update((c) => ({ ...c, buttonDebounceMs: v }))}
            min={L.buttonDebounceMs[0]}
            max={L.buttonDebounceMs[1]}
            step={5}
            unit="ms"
          />
          <label className="block sm:col-span-2">
            <span className="text-xs font-medium text-zinc-400">Buzzer volume · {d.buzzerVolumePct}%</span>
            <input
              type="range"
              min={L.buzzerVolumePct[0]}
              max={L.buzzerVolumePct[1]}
              step={5}
              value={d.buzzerVolumePct}
              onChange={(e) => cfg.update((c) => ({ ...c, buzzerVolumePct: Number(e.target.value) }))}
              className="mt-2 w-full accent-emerald-500"
            />
            <span className="block text-xs text-zinc-500">0% mutes everything except the E-STOP alarm.</span>
          </label>
        </div>
      </Card>

      <ConfigBar id="shredder" sync={cfg.sync} version={cfg.version} connected={connected} />
    </ModulePanel>
  );
}
