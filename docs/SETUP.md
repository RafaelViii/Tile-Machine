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
3. ✅ Email/Password sign-in enabled.
4. ✅ Users:
   - admin: `rafaelvberinguelajr@gmail.com`, uid `EJ1xMe2WYfbYppddoNmXrX7e1H73`
   - hub: `hub@tile-machine.local`, uid `aVibq4sfUOSFcDzsQC7zZ6xykX92` (password only in the
     git-ignored `firmware/hub/include/secrets.h`)
5. ✅ Roles written with the CLI (`firebase database:set /roles …`):
   ```
   roles/EJ1xMe2WYfbYppddoNmXrX7e1H73 = "admin"
   roles/aVibq4sfUOSFcDzsQC7zZ6xykX92 = "hub"
   ```
   To add another admin later: create the user in the console, then
   `firebase database:set /roles/<uid> '"admin"'` or add it in Realtime Database → Data.
6. ✅ Web app registered. The config is in `web/.env.local` (git-ignored, template
   `web/.env.example`). Analytics is not used.
7. Copy the **Web API key** and **Database URL** into `firmware/hub/include/secrets.h` (template:
   `secrets.example.h`, created in Phase 1), together with the hub email/password, your WiFi and a `PORTAL_PASSWORD` (8+ characters) for the
   hub's setup hotspot. The WiFi in `secrets.h` is only the first saved network: later networks
   are added from the setup hotspot `TileHub-XXXX` (open for 3 min after every restart, or press
   BOOT briefly), see docs/modules/hub.md "WiFi and setup hotspot".
8. ✅ Rules deployed and checked: an anonymous user is blocked, the hub can write `/hub` and read
   `/modules`, and the hub can't write config or change roles. To redeploy after editing
   `firebase/database.rules.json` (the CLI is already logged in as the admin Google account):
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
