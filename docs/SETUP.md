# Setup Guide

## 1. GitHub (version control)

1. On github.com → **New repository** → name `tile-machine`, **Private**. **Don't** add a README,
   .gitignore or license (the repo already has them).
2. Copy the repo URL (e.g. `https://github.com/<you>/tile-machine.git`).
3. In the project folder:
   ```bash
   git remote add origin https://github.com/<you>/tile-machine.git
   git push -u origin main
   ```
   The first push opens the Git Credential Manager browser login.

## 2. Tools

| Tool | Install |
|---|---|
| PlatformIO | VS Code → Extensions → **PlatformIO IDE** (includes the `pio` CLI) |
| Node.js | Already installed (v20). |
| Firebase CLI | `npm install -g firebase-tools`, then `firebase login` |
| USB driver | CP210x or CH340, depending on your ESP32 DevKit's USB chip |

## 3. Firebase project

1. <https://console.firebase.google.com> → **Add project** → e.g. `tile-machine`. Google
   Analytics isn't needed. The Spark (free) plan is enough.
2. **Build → Realtime Database → Create database**. Pick the region closest to you. Start in
   **locked mode**.
3. **Build → Authentication → Get started → Email/Password → Enable**.
4. **Authentication → Users → Add user**, twice:
   - you (web admin), e.g. your email + a strong password
   - the hub, e.g. `hub@tile-machine.local` + a long random password
   Copy both **User UIDs**.
5. **Realtime Database → Data**: create
   ```
   roles/<your-uid> = "admin"
   roles/<hub-uid>  = "hub"
   ```
6. **Project settings → General → Your apps → Web (</>)** → register the app `tile-machine-web`.
   Copy the config values into `web/.env.local` (template: `web/.env.example`, created in
   Phase 2).
7. Copy the **Web API key** and **Database URL** into `firmware/hub/include/secrets.h` (template:
   `secrets.example.h`, created in Phase 1/2), together with the hub email/password and your WiFi.
8. In the repo root: copy `.firebaserc.example` → `.firebaserc` and put in your project id, then
   ```bash
   firebase deploy --only database
   ```

## 4. WiFi router

- Set the 2.4 GHz band to a **fixed channel** (1, 6 or 11). ESP-NOW follows the router's channel.
  With "auto channel", every channel change makes the modules re-pair (automatic, but a few
  seconds of "offline").
- ESP32 only supports **2.4 GHz**.

## 5. First flash order (Phase 1 onward)

1. Hub → check serial: WiFi connected, channel N, ESP-NOW ready.
2. Each module → check serial: `[NET] scanning…` → `[NET] paired with hub on channel N`.
