import type { ComponentType, SVGProps } from 'react';
import { Link } from 'react-router';
import { ContainerIcon, HotpressIcon, HubIcon, ShredderIcon } from '../../components/icons';
import { Badge, Card, CardTitle, Lamp, Stat } from '../../components/ui';
import { ago, faultList, uptime } from '../../shared/format';
import { useMachine, type MachineStatus } from '../../shared/machine';
import type { ModuleId } from '../../shared/types/rtdb';

type Tone = 'green' | 'amber' | 'red' | 'zinc' | 'sky';

function StatusBadge({ state, label }: { state: 'on' | 'off' | 'warn' | 'fault'; label: string }) {
  const tone: Tone = state === 'on' ? 'green' : state === 'warn' ? 'amber' : state === 'fault' ? 'red' : 'zinc';
  return (
    <Badge tone={tone}>
      <Lamp state={state} /> {label}
    </Badge>
  );
}

function DeviceTitle({ Icon, name, esp }: { Icon: ComponentType<SVGProps<SVGSVGElement>>; name: string; esp: string }) {
  return (
    <span className="flex items-center gap-2">
      <Icon className="h-4 w-4 text-zinc-400" />
      {name}
      <span className="font-mono text-xs font-normal tracking-normal text-zinc-500 normal-case">· {esp}</span>
    </span>
  );
}

function HubCard({ m }: { m: MachineStatus }) {
  const { hub, hubOnline, now } = m;
  const clock =
    hub?.rtc === 'ok'
      ? `RTC ok · ${hub.timeSource === 'ntp' ? 'internet' : hub.timeSource === 'rtc' ? 'RTC only' : '—'}`
      : hub?.rtc === 'lost-power'
        ? 'RTC not set'
        : hub?.rtc === 'missing'
          ? 'RTC missing'
          : '—';
  return (
    <Card>
      <CardTitle
        right={<StatusBadge state={hubOnline ? 'on' : hub ? 'fault' : 'off'} label={hubOnline ? 'Online' : hub ? 'Offline' : 'Never seen'} />}
      >
        <DeviceTitle Icon={HubIcon} name="Main hub" esp="esp0" />
      </CardTitle>
      {!hub ? (
        <p className="text-sm text-zinc-400">
          The hub hasn't reported yet. Once it's on WiFi, it shows up here and the modules start lighting up.
        </p>
      ) : (
        <div className="grid grid-cols-2 gap-2.5 lg:grid-cols-4">
          <Stat label="Last seen" value={ago(hub.lastSeen, now)} tone={hubOnline ? 'green' : 'red'} />
          <Stat label="Up since" value={hub.bootAt ? uptime(Math.round((now - hub.bootAt) / 1000)) : '—'} />
          <Stat label="IP" value={hub.ip ?? '—'} />
          <Stat label="Firmware" value={hub.fw ?? '—'} />
          <Stat label="WiFi channel" value={hub.wifiChannel ?? '—'} />
          <Stat label="WiFi signal" value={hub.wifiRssi !== undefined ? `${hub.wifiRssi} dBm` : '—'} />
          <Stat
            label="Clock (DS3231)"
            value={clock}
            tone={hub.rtc === 'ok' ? 'green' : hub.rtc ? 'amber' : undefined}
          />
          <Stat label="Protocol" value={hub.protocolVersion !== undefined ? `v${hub.protocolVersion}` : '—'} />
        </div>
      )}
    </Card>
  );
}

function configSync(m: MachineStatus, id: ModuleId): { text: string; tone?: Tone } {
  const node = m.modules[id].node;
  const cfg = node?.config;
  const applied = node?.configApplied;
  if (!cfg) {
    const v = node?.state?.configVersion;
    return { text: v ? `Board v${v}` : 'Defaults' };
  }
  if (applied?.version === cfg.version) {
    if (applied.result === 'rejected') return { text: `Rejected v${cfg.version}`, tone: 'red' };
    if (applied.result === 'queued') return { text: `Queued v${cfg.version}`, tone: 'amber' };
    return { text: `Synced v${cfg.version}`, tone: 'green' };
  }
  return { text: `Pending v${cfg.version}`, tone: 'amber' };
}

function ModuleCard({
  m,
  id,
  name,
  esp,
  Icon,
  to,
}: {
  m: MachineStatus;
  id: ModuleId;
  name: string;
  esp: string;
  Icon: ComponentType<SVGProps<SVGSVGElement>>;
  to: string;
}) {
  const { node, connected } = m.modules[id];
  const faults = faultList(id, node?.state?.faults);
  const everPaired = !!node?.info;
  const lamp = connected ? (faults.length ? 'warn' : 'on') : everPaired ? 'fault' : 'off';
  const label = connected ? (faults.length ? 'Online · fault' : 'Online') : everPaired ? 'Offline' : 'Not paired yet';
  const sync = configSync(m, id);

  return (
    <Card>
      <CardTitle right={<StatusBadge state={lamp} label={label} />}>
        <Link to={to} className="hover:text-hazard">
          <DeviceTitle Icon={Icon} name={name} esp={esp} />
        </Link>
      </CardTitle>
      {!everPaired ? (
        <p className="text-sm text-zinc-400">
          Flash this ESP32 and power it on near the hub. It pairs by itself and appears here. No buttons needed.
        </p>
      ) : (
        <div className="grid grid-cols-2 gap-2.5 lg:grid-cols-4">
          <Stat label="Last seen" value={ago(node?.presence?.lastSeen, m.now)} tone={connected ? 'green' : 'red'} />
          <Stat label="Uptime" value={connected ? uptime(node?.state?.uptimeS) : '—'} />
          <Stat label="MAC" value={node?.info?.mac ?? '—'} />
          <Stat label="Firmware" value={node?.info?.fw ?? '—'} />
          <Stat label="Linked" value={ago(node?.info?.pairedAt, m.now)} />
          <Stat label="Config" value={sync.text} tone={sync.tone} />
          <Stat
            label="Faults"
            value={connected ? (faults.length ? faults.join(', ') : 'None') : '—'}
            tone={connected ? (faults.length ? 'amber' : 'green') : undefined}
          />
          <Stat
            label="Safety"
            value={connected ? (node?.state?.interlock ? 'Interlock: set switch OFF' : 'Normal') : '—'}
            tone={connected ? (node?.state?.interlock ? 'amber' : 'green') : undefined}
          />
        </div>
      )}
    </Card>
  );
}

/** Every ESP32 in the system, with its connection and health. */
export function DeviceCards() {
  const m = useMachine();
  const online =
    (m.hubOnline ? 1 : 0) + (['shredder', 'containing', 'hotpress'] as const).filter((id) => m.modules[id].connected).length;

  return (
    <section>
      <div className="mb-3 flex items-end justify-between gap-2">
        <h2 className="font-display text-xl font-bold tracking-wider uppercase">Devices</h2>
        <span className="font-mono text-xs text-zinc-500">{online} / 4 ESP32 online</span>
      </div>
      <div className="grid gap-4">
        <HubCard m={m} />
        <ModuleCard m={m} id="shredder" name="Shredder" esp="esp1" Icon={ShredderIcon} to="/shredder" />
        <ModuleCard m={m} id="containing" name="Containing" esp="esp2" Icon={ContainerIcon} to="/containing" />
        <ModuleCard m={m} id="hotpress" name="Hot Press · Designing · Curing" esp="esp3" Icon={HotpressIcon} to="/hotpress" />
      </div>
    </section>
  );
}
