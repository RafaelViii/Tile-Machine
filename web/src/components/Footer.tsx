import { useMachine } from '../shared/machine';
import { LogoMark } from './icons';

const LINE = ['Shredder', 'Containing', 'Hot Press', 'Designing', 'Curing'];

/** Quiet page footer: what Tile Console and the Tile Machine are, the process line, the safety rule. */
export function Footer() {
  const { hub } = useMachine();
  return (
    <footer className="mt-12 border-t border-zinc-800">
      <div className="mx-auto grid max-w-6xl gap-8 px-4 py-10 sm:grid-cols-[1.5fr_1fr_1fr]">
        <div>
          <div className="flex items-center gap-2">
            <LogoMark className="h-6 w-6" />
            <span className="font-semibold">Tile Console</span>
          </div>
          <p className="mt-3 max-w-sm text-xs leading-relaxed text-zinc-500">
            The control panel for the Tile Machine: a plastic-recycling line that shreds HDPE and PP, measures it
            out and presses it into tiles. Each station runs on its own controller and keeps working without this
            website.
          </p>
        </div>
        <div>
          <div className="text-xs font-medium text-zinc-400">The line</div>
          <ol className="mt-3 space-y-1.5 text-xs text-zinc-500">
            {LINE.map((s, i) => (
              <li key={s} className="flex items-center gap-2">
                <span className="w-4 text-zinc-600 tabular-nums">{i + 1}</span>
                {s}
              </li>
            ))}
          </ol>
        </div>
        <div>
          <div className="text-xs font-medium text-zinc-400">Safety</div>
          <p className="mt-3 text-xs leading-relaxed text-zinc-500">
            This website can stop the machine but never start it. Starting always needs a person at the machine, and
            the STOP buttons on the machine always win.
          </p>
        </div>
      </div>
      <div className="mx-auto flex max-w-6xl flex-wrap justify-between gap-2 border-t border-zinc-800/60 px-4 py-5 text-xs text-zinc-600">
        <span>© {new Date().getFullYear()} Tile Machine</span>
        <span className="tabular-nums">{hub?.fw ? `Hub firmware ${hub.fw}` : 'Hub not connected yet'}</span>
      </div>
    </footer>
  );
}
