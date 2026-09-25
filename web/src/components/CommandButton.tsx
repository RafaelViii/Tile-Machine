import { useEffect, type ComponentType, type ReactNode, type SVGProps } from 'react';
import { useCommand } from '../shared/hooks/useCommand';
import type { CommandType, ModuleId } from '../shared/types/rtdb';
import { AlertIcon, CalibrateIcon, CheckIcon, IdentifyIcon, RebootIcon, SpinnerIcon, StopGlyph, TareIcon } from './icons';
import { Button, cx } from './ui';

const GLYPH: Record<CommandType, ComponentType<SVGProps<SVGSVGElement>>> = {
  STOP: StopGlyph,
  IDENTIFY: IdentifyIcon,
  REBOOT: RebootIcon,
  TARE: TareIcon,
  CALIBRATE: CalibrateIcon,
};

// Read out by screen readers and shown as the tooltip; the button itself only shows an icon.
const statusText: Record<string, string> = {
  sending: 'Sending…',
  pending: 'Waiting for the hub…',
  sent: 'Delivered, waiting for the module…',
  done: 'Done',
  failed: 'Failed: the module did not carry it out',
  expired: 'Expired: the hub got it too late',
  noanswer: 'No answer from the hub. Use the physical STOP if needed',
  error: 'Could not send',
};

const DONE_SHOW_MS = 2000;

/**
 * Sends one command. Its progress is shown by the button's own icon, so nothing appears around it:
 * the command glyph → a spinner while it travels → ✓ for 2 s → back to the glyph. On failure a warning
 * icon stays (tooltip says why) until the button is pressed again.
 */
export function CommandButton({
  moduleId,
  type,
  target,
  arg,
  variant = 'subtle',
  disabled,
  disabledReason,
  children,
  className,
}: {
  moduleId: ModuleId | 'all';
  type: CommandType;
  target?: number;
  arg?: number;
  variant?: 'primary' | 'danger' | 'ghost' | 'subtle';
  disabled?: boolean;
  disabledReason?: string;
  children: ReactNode;
  className?: string;
}) {
  const { status, error, send, reset } = useCommand(moduleId);

  useEffect(() => {
    if (status !== 'done') return;
    const t = setTimeout(reset, DONE_SHOW_MS);
    return () => clearTimeout(t);
  }, [status, reset]);

  const busy = status === 'sending' || status === 'pending' || status === 'sent';
  const bad = status === 'failed' || status === 'expired' || status === 'noanswer' || status === 'error';
  const Glyph = GLYPH[type];
  const text = status ? statusText[status] + (error ? `: ${error}` : '') : '';
  const title = disabled ? disabledReason : bad ? text : undefined;

  return (
    <Button
      variant={variant}
      disabled={disabled || status === 'sending'}
      title={title}
      onClick={() => send(type, { target, arg })}
      className={className}
    >
      <span className="relative grid h-4 w-4 shrink-0 place-items-center" aria-hidden>
        {busy ? (
          <SpinnerIcon className="h-4 w-4 animate-spin" />
        ) : status === 'done' ? (
          <CheckIcon className="h-4 w-4" strokeWidth={2.5} />
        ) : bad ? (
          <AlertIcon className={cx('h-4 w-4', variant !== 'danger' && 'text-amber-300')} strokeWidth={2} />
        ) : (
          <Glyph className={type === 'STOP' ? 'h-3.5 w-3.5' : 'h-4 w-4'} />
        )}
      </span>
      {children}
      <span className="sr-only" role="status" aria-live="polite">
        {text}
      </span>
    </Button>
  );
}
