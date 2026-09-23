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
