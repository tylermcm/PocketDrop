# What a host provides to `PocketDropView`

The view is pure: it draws state and sends commands. It never touches files, sockets or settings itself.

## Context properties

- `theme` — colours, an object with the names in `theme.json` (`base s1 s2 s3 line lineSoft fg fg2 fg3 accent accentFg onAccent win rev rej lift`). A `QQmlPropertyMap` filled from `theme.json` works; Image Triage passes its own `theme`.
- `pocketDrop` — the backend:
  - `st` (`QVariantMap`): the engine's `Ui::stateJson()` parsed. Fields the view reads: `mode status tone card cardMsg pct qrSize qrPath hasLink canOpen linkText linkEmpty copied copyLabel info rows menu pc offline`.
  - `rows` (`QVariantList`): the list to show (the engine's rows; a host may merge its own, such as Image Triage's Library groups).
  - `riskPending` (bool), `confirmRisk(proceed, dontAskAgain)`.
  - `start()`, `command(name, arg)`, `setMode(int)`, `addUrls(list)`, `addText(text)`, `removeRow(key)`, `clearAll()`, `changeInbox(url)`.
- Commands are the engine's (`Ui::command`): `copy open_link new_link share paste card addr row menu pc_send pc_receive pc_stop pc_action pc_pick pc_allow pc_decline pc_send_to pc_invite_accept pc_invite_decline history_open`, and `menu` with `open_inbox burn warn_any topmost stop_tunnel about note_clip forget_phones history_clear visible`.

## Extension points

- `showTitle` — the standalone window shows the name; a host with its own header turns it off.
- `handleRisk` — the view asks before Anywhere; a host that asks itself turns it off.
- `extraButtons` — buttons beside Add (Image Triage adds Library).
- `menuExtras` — entries in the menu's settings group.
