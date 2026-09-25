import { ConfigBar } from '../../components/ConfigBar';
import { ModulePanel } from '../../components/ModulePanel';
import { NumberField } from '../../components/NumberField';
import { CuringIcon, DesignIcon, HotpressIcon } from '../../components/icons';
import { Badge, Card, CardTitle, EmptyNote, Stat, cx } from '../../components/ui';
import { LIMITS } from '../../shared/configDefaults';
import { useConfigEditor } from '../../shared/hooks/useConfigEditor';
import { useMachine } from '../../shared/machine';

const L = LIMITS.hotpress;

function RelayTile({ name, on, detail, Icon }: {
  name: string;
  on: boolean;
  detail: string;
  Icon: typeof HotpressIcon;
}) {
  return (
    <div
      className={cx(
        'flex flex-col items-center gap-2 rounded-2xl border p-5 text-center',
        on ? 'border-orange-500/50 bg-orange-500/[0.08]' : 'border-zinc-800 bg-zinc-950/60',
      )}
    >
      <Icon className={cx('h-9 w-9', on ? 'text-orange-300' : 'text-zinc-500')} />
      <div className="font-semibold">{name}</div>
      <div className={cx('text-sm font-medium', on ? 'text-orange-300' : 'text-zinc-400')}>{detail}</div>
    </div>
  );
}

export function HotpressPage() {
  const { modules } = useMachine();
  const { node, connected } = modules.hotpress;
  const s = connected ? node?.state : undefined;
  const cfg = useConfigEditor('hotpress', node?.config ?? undefined, node?.configApplied);
  const d = cfg.draft;

  const hotpressDetail = !s
    ? '—'
    : s.selector === 'RIGHT'
      ? 'AUTO (placeholder: on)'
      : s.relayHotpress
        ? 'HEATING'
        : 'COOLING';

  return (
    <ModulePanel
      id="hotpress"
      title="Hot Press"
      subtitle="Hot Press · Designing · Curing (one ESP32, 2 relays)"
      esp="esp3"
    >
      <Card className="mb-6">
        <CardTitle>Live status</CardTitle>
        {s ? (
          <>
            <div className="grid gap-3 sm:grid-cols-3">
              <RelayTile name="Hot Press" on={s.relayHotpress} detail={hotpressDetail} Icon={HotpressIcon} />
              <RelayTile name="Designing" on={s.relayDesignCure} detail={s.relayDesignCure ? 'ON' : 'OFF'} Icon={DesignIcon} />
              <RelayTile name="Curing" on={s.relayDesignCure} detail={s.relayDesignCure ? 'ON' : 'OFF'} Icon={CuringIcon} />
            </div>
            <div className="mt-4 grid grid-cols-2 gap-3 sm:grid-cols-4">
              <Stat label="ON button" value={s.onButton ? 'ON' : 'OFF'} tone={s.onButton ? 'green' : undefined} />
              <Stat
                label="Selector"
                value={s.selector === 'LEFT' ? 'LEFT · Hotpress' : s.selector === 'RIGHT' ? 'RIGHT · Auto' : 'MIDDLE · Cooling'}
                tone="sky"
              />
              <Stat label="Relay 1 (Design+Cure)" value={s.relayDesignCure ? 'ON' : 'OFF'} />
              <Stat label="Relay 2 (Hotpress)" value={s.relayHotpress ? 'ON' : 'OFF'} />
            </div>
            {s.stopLatched && (
              <p className="mt-4 rounded-lg bg-red-500/10 px-3 py-2 text-sm text-red-300 ring-1 ring-red-500/30">
                Stopped remotely. Turn the ON button OFF and the selector to the middle to reset.
              </p>
            )}
          </>
        ) : (
          <EmptyNote>Live status appears here once the station connects.</EmptyNote>
        )}
      </Card>

      <Card className="mb-6">
        <CardTitle>Inputs</CardTitle>
        <div className="grid gap-5 sm:grid-cols-2">
          <NumberField
            label="ON button debounce"
            hint="A new button position only counts after it stays steady this long. Raise it if Designing + Curing flickers when pressed"
            value={d.buttonDebounceMs}
            onChange={(v) => cfg.update((c) => ({ ...c, buttonDebounceMs: v }))}
            min={L.buttonDebounceMs[0]}
            max={L.buttonDebounceMs[1]}
            step={10}
            unit="ms"
          />
          <NumberField
            label="Selector debounce"
            hint="Same for the 3-way selector (Hot Press heating / cooling / auto)"
            value={d.selectorDebounceMs}
            onChange={(v) => cfg.update((c) => ({ ...c, selectorDebounceMs: v }))}
            min={L.selectorDebounceMs[0]}
            max={L.selectorDebounceMs[1]}
            step={10}
            unit="ms"
          />
        </div>
        <p className="mt-3 text-[11px] text-zinc-500">
          Applied right away: a longer filter never turns anything on, it only ignores contact chatter.
        </p>
      </Card>

      <Card>
        <CardTitle right={<Badge tone="amber">Not designed yet</Badge>}>AUTO mode</CardTitle>
        <p className="text-sm text-zinc-400">
          Selector RIGHT shows <b>AUTO</b>. For now the Hot Press just stays on, the same as LEFT. The automatic logic
          (temperature, timed press cycle, or triggered by Containing) will be added once it's decided.
        </p>
      </Card>

      <ConfigBar id="hotpress" sync={cfg.sync} version={cfg.version} connected={connected} />
    </ModulePanel>
  );
}
