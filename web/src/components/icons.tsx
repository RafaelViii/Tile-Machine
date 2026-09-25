import type { SVGProps } from 'react';

type P = SVGProps<SVGSVGElement>;
const base = {
  viewBox: '0 0 24 24',
  fill: 'none',
  stroke: 'currentColor',
  strokeWidth: 1.7,
  strokeLinecap: 'round' as const,
  strokeLinejoin: 'round' as const,
};

export const ShredderIcon = (p: P) => (
  <svg {...base} {...p}>
    <path d="M4 4h16l-2 7H6z" />
    <circle cx="9" cy="15" r="2.5" />
    <circle cx="15" cy="15" r="2.5" />
    <path d="M8 20l-1 1M12 20v1.5M16 20l1 1" />
  </svg>
);

export const ContainerIcon = (p: P) => (
  <svg {...base} {...p}>
    <ellipse cx="12" cy="5" rx="7" ry="2.5" />
    <path d="M5 5v11c0 1.4 3.1 2.5 7 2.5s7-1.1 7-2.5V5" />
    <path d="M10 18.5v2.5h4v-2.5" />
  </svg>
);

export const HotpressIcon = (p: P) => (
  <svg {...base} {...p}>
    <rect x="4" y="3" width="16" height="4" rx="1" />
    <rect x="4" y="17" width="16" height="4" rx="1" />
    <path d="M9 10c0 1.5 1.5 1.5 1.5 3S9 14.5 9 16M14 10c0 1.5 1.5 1.5 1.5 3s-1.5 1.5-1.5 3" />
  </svg>
);

export const DesignIcon = (p: P) => (
  <svg {...base} {...p}>
    <rect x="3.5" y="3.5" width="7" height="7" rx="1" />
    <rect x="13.5" y="3.5" width="7" height="7" rx="1" />
    <rect x="3.5" y="13.5" width="7" height="7" rx="1" />
    <path d="M13.5 17h7M17 13.5v7" />
  </svg>
);

export const CuringIcon = (p: P) => (
  <svg {...base} {...p}>
    <circle cx="12" cy="13" r="8" />
    <path d="M12 9v4l2.5 2M9.5 2.5h5" />
  </svg>
);

export const HubIcon = (p: P) => (
  <svg {...base} {...p}>
    <circle cx="12" cy="12" r="2.5" />
    <path d="M7.8 7.8a6 6 0 0 0 0 8.4M16.2 7.8a6 6 0 0 1 0 8.4M5 5a10 10 0 0 0 0 14M19 5a10 10 0 0 1 0 14" />
  </svg>
);

export const ArrowRight = (p: P) => (
  <svg {...base} {...p}>
    <path d="M5 12h14M13 6l6 6-6 6" />
  </svg>
);

export const ChevronDown = (p: P) => (
  <svg {...base} {...p}>
    <path d="M6 9l6 6 6-6" />
  </svg>
);

export const PencilIcon = (p: P) => (
  <svg {...base} {...p}>
    <path d="M4 20h4L19 9l-4-4L4 16z" />
    <path d="M13.5 6.5l4 4" />
  </svg>
);

export const TrashIcon = (p: P) => (
  <svg {...base} {...p}>
    <path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3" />
  </svg>
);

export const CheckIcon = (p: P) => (
  <svg {...base} {...p}>
    <path d="M5 12.5l4.5 4.5L19 7.5" />
  </svg>
);

export const PresetIcon = (p: P) => (
  <svg {...base} {...p}>
    <path d="M5 6h9M18 6h1M5 12h3M12 12h7M5 18h11M20 18h-1" />
    <circle cx="16" cy="6" r="2" />
    <circle cx="10" cy="12" r="2" />
    <circle cx="18" cy="18" r="2" />
  </svg>
);

export const SunMoonIcon = (p: P) => (
  <svg {...base} {...p}>
    <circle cx="12" cy="12" r="4" />
    <path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4" />
  </svg>
);

export const LogoutIcon = (p: P) => (
  <svg {...base} {...p}>
    <path d="M15 4h3a2 2 0 012 2v12a2 2 0 01-2 2h-3M10 16l-4-4 4-4M6 12h10" />
  </svg>
);

/**
 * Brand mark: four tiles in the app's line style; the last one is the finished tile.
 * `raised` lifts that tile out of the grid (darker edge underneath). Only use it at 32 px or
 * more: at smaller sizes the edge is ~1 px and reads as a smudge.
 */
export const LogoMark = ({ raised, ...p }: P & { raised?: boolean }) => (
  <svg viewBox="0 0 24 24" fill="none" aria-hidden {...p}>
    <g stroke="currentColor" strokeWidth={1.7} strokeLinejoin="round" className="text-zinc-400">
      <rect x="3" y="3" width="7.5" height="7.5" rx="2" />
      <rect x="13.5" y="3" width="7.5" height="7.5" rx="2" />
      <rect x="3" y="13.5" width="7.5" height="7.5" rx="2" />
    </g>
    {raised ? (
      <>
        <rect x="13.4" y="13.4" width="8.5" height="8.5" rx="2.6" className="fill-emerald-800" />
        <rect x="12.1" y="12.1" width="8.5" height="8.5" rx="2.6" className="fill-emerald-400" />
      </>
    ) : (
      // Same outer size as the stroked tiles (stroke adds 0.85 on each side).
      <rect x="12.65" y="12.65" width="9.2" height="9.2" rx="2.85" className="fill-emerald-400" />
    )}
  </svg>
);
