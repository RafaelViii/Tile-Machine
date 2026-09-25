import type { ReactNode } from 'react';
import { useCommand } from '../shared/hooks/useCommand';
import type { CommandType, ModuleId } from '../shared/types/rtdb';
import { Button, cx } from './ui';

const statusText: Record<string, string> = {
  sending: 'Sending…',
  pending: 'Waiting for hub…',
  sent: 'Delivered to module…',
  done: 'Done ✓',
  failed: 'Failed',
  expired: 'Expired (hub too late)',
  error: 'Error',
};

/** Sends one command and shows its live status underneath. */
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
  floatingStatus,
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
  /** Show the status as a small label under the button without changing the layout (header use). */
  floatingStatus?: boolean;
}) {
  const { status, error, send } = useCommand(moduleId);
  return (
    <div className={floatingStatus ? 'relative' : 'flex flex-col items-start gap-1'}>
      <Button
        variant={variant}
        disabled={disabled || status === 'sending'}
        title={disabled ? disabledReason : undefined}
        onClick={() => send(type, { target, arg })}
        className={className}
      >
        {children}
      </Button>
      {status && (
        <span
          className={cx(
            'text-xs',
            floatingStatus && 'absolute top-full right-0 mt-1 whitespace-nowrap',
            status === 'done'
              ? 'text-emerald-400'
              : status === 'failed' || status === 'expired' || status === 'error'
                ? 'text-red-400'
                : 'text-zinc-400',
          )}
        >
          {statusText[status]}
          {error ? `: ${error}` : ''}
        </span>
      )}
    </div>
  );
}
