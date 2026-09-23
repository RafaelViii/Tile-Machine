# Web Dashboard

Live: **https://tile-machine-92345.web.app** (sign in with an account that has `roles/<uid> = "admin"`).

React 19 + Vite 8 + TypeScript + Tailwind 4 + Firebase JS SDK 12, hosted on Firebase Hosting.
Data contract: `docs/DATA_MODEL.md` (mirrored in `src/shared/types/rtdb.ts`).

## Develop

```bash
cd web
npm install
npm run dev          # http://localhost:5173 (uses the real Firebase project via .env.local)
npm run build        # type-check + production build → web/dist
```

`.env.local` (git-ignored) holds the Firebase web config. The template is `.env.example`.

## Deploy

```bash
cd web && npm run build && cd .. && firebase deploy --only hosting
```

## Structure

| Path | What |
|---|---|
| `src/lib/firebase.ts` | Firebase init (the only place that reads env vars) |
| `src/shared/types/rtdb.ts` | Types for the RTDB tree |
| `src/shared/configDefaults.ts` | Config defaults + allowed ranges (mirrors `docs/PROTOCOL.md` §6) |
| `src/shared/machine.tsx` | One live subscription to hub + all modules. `connected = hubOnline && presence.online` |
| `src/shared/hooks/` | `useValue` (live path), `useServerNow`, `useCommand` (send + follow status), `useConfigEditor` (draft + versioned save) |
| `src/components/` | UI primitives, `SlideToggle`, `NumberField`, `CommandButton`, `ConfigBar`, `ModulePanel`, `Layout` |
| `src/features/*` | Pages: auth, dashboard, shredder, containing, hotpress, events |

## Behaviour notes

- A process tile lights up only when the hub is online (`/hub/lastSeen` < 25 s old by server time)
  **and** the module's `presence.online` is true. Hotpress lights up Hot Press, Designing and Curing.
- Saving a config runs a transaction that bumps `version`. The badge moves Saving… → Saved ·
  waiting for module → Synced to module ✓ (when the hub writes `configApplied`).
- The web can only send `STOP` / `IDENTIFY` / `TARE` / `CALIBRATE` / `REBOOT`. Starting is
  physical-only by design. Command buttons are disabled while the hub/module is offline.
