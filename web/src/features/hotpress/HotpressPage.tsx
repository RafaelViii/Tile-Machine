import { ModulePanel } from '../../components/ModulePanel';
import { CuringIcon, DesignIcon, HotpressIcon } from '../../components/icons';
import { Badge, Card, CardTitle, EmptyNote, Stat, cx } from '../../components/ui';
import { useMachine } from '../../shared/machine';

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

      <Card>
        <CardTitle right={<Badge tone="amber">Not designed yet</Badge>}>AUTO mode</CardTitle>
        <p className="text-sm text-zinc-400">
          Selector RIGHT shows <b>AUTO</b>. For now the Hot Press just stays on, the same as LEFT. The automatic logic
          (temperature, timed press cycle, or triggered by Containing) will be added once it's decided.
        </p>
      </Card>
    </ModulePanel>
  );
}
