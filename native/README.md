# separate_roll_and_sprint

Native ME3 bridge for split dodge (press-roll) and INI-driven sprint. **No `libER.dll` at runtime.**

One INI supports keyboard and controller together. **Keyboard `dash_key` is sprint-only** (never crouch). **Controller `dash_button`** uses stick gating: neutral stick toggles crouch/stealth in HKS; tilted stick sprints. Sprint activates if keyboard **or** tilted controller dash is held.

## Build

```powershell
cmake -S native -B native/build
cmake --build native/build --config Release
```

Output (Release):

- `native/build/Release/separate_roll_and_sprint.dll`
- `native/build/Release/separate_roll_and_sprint.ini` (copied post-build)

Release builds run `dumpbin /DEPENDENTS` on the DLL; confirm `libER.dll` is not listed.

## Deploy (ME3 bind folder)

| File | Purpose |
|------|---------|
| `separate_roll_and_sprint.dll` | HKS native + keyboard/XInput polling |
| `separate_roll_and_sprint.ini` | Unified config |
| `bind/mod/.../c0000.hks` | Press-roll + sprint script |

Log: `separate_roll_and_sprint.log`. Hotkey: **F10** reloads INI.

Legacy INI name `ERKeyAssignDashInputBridge.ini` is still read if the new file is missing.

## INI example

```ini
; Dash activates if any configured input below is held.
dash_key=LeftShift
dash_button=L3
dash_trigger_threshold=80
gamepad_index=any
left_stick_dash_deadzone=12000
; dash_key (keyboard): sprint only — does not toggle crouch.
; dash_button (controller): neutral stick = crouch/stealth (HKS), tilted stick = sprint.
```

- `dash_key` — optional keyboard (`LeftShift`, `Space`, `F`, `A`–`Z`, …). **Sprint only**; never toggles crouch.
- `dash_button` — optional controller (`L1`/`LB`, `L3`/`LS`, `Cross`/`A`, `L2`, `R2`, …).
- **Controller `dash_button`:** Native stick-gates sprint (`left_stick_dash_deadzone`). Button held + neutral stick → `ERSplit_IsDashButtonNeutralHeldNative` (controller-only; HKS toggles crouch/stealth with lockout/rearm). Button held + tilted stick → `ERSplit_IsDashButtonHeldNative` (sprint). After neutral crouch, sprint stays blocked until controller dash button release; keyboard sprint is not blocked after controller release. After controller sprint release, HKS applies a short reactivation cooldown (`ERSPLIT_CONTROLLER_SPRINT_COOLDOWN_FRAMES`, default 6 frames); keyboard sprint is unaffected. Works for L3, L1, Cross, DPad, etc. Do not bind the same physical button elsewhere.
- `gamepad_index` — `any` (users 0–3) or `0`–`3`.
- `dash_trigger_threshold` — `0`–`255` for `L2`/`R2` (default `80`).

## Controller note (XInput)

Windows exposes controllers through **XInput** (Xbox-style low-level buttons). PlayStation names in the INI are **aliases** for the same inputs (`L1` = `LB` = left shoulder, `Cross` = `A`, etc.). PlayStation controllers must be mapped through Steam Input, DS4Windows, or another XInput-compatible layer.

XInput is loaded dynamically (`xinput1_4.dll` → `xinput1_3.dll` → `xinput9_1_0.dll`). If unavailable, keyboard `dash_key` still works.
