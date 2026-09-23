# Setup Guide

## 1. GitHub (version control)

Repo: <https://github.com/RafaelViii/Tile-Machine> (already connected as `origin`, branch `main`).

To clone it on another PC:
```bash
git clone https://github.com/RafaelViii/Tile-Machine.git
```

## 2. Tools

| Tool | Install |
|---|---|
| PlatformIO | VS Code → Extensions → **PlatformIO IDE** (includes the `pio` CLI) |
| Node.js | Already installed (v20). |
| Firebase CLI | `npm install -g firebase-tools`, then `firebase login` |
| USB driver | CP210x or CH340, depending on your ESP32 DevKit's USB chip |

## 3. Firebase project

Project: **`tile-machine-92345`**, Realtime Database in **asia-southeast1**.

1. ✅ Project created.
2. ✅ Realtime Database created. It should be in **locked mode** until our rules are deployed.
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
6. ✅ Web app registered. The config is in `web/.env.local` (git-ignored, template
   `web/.env.example`). Analytics is not used.
7. Copy the **Web API key** and **Database URL** into `firmware/hub/include/secrets.h` (template:
   `secrets.example.h`, created in Phase 1), together with the hub email/password and your WiFi.
8. Deploy the security rules from the repo root (`.firebaserc` already points to the project):
   ```bash
   npm install -g firebase-tools
   firebase login
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
