# ER Split Instant Dodge & Separate Sprint

Elden Ring Mod Engine 3 mod that separates **dodge roll** (press) from **sprint** (hold) on a single bind, for keyboard and controller.

- **Roll on press** while moving — uses vanilla roll / self-transition behavior.
- **Sprint on hold** via a dedicated dash key or controller dash button.
- **No `libER.dll` at runtime** — only `separate_roll_and_sprint.dll` plus HKS.

## How It Works

`c0000.hks` routes moving dodge input through `ACTION_ARM_SP_MOVE` so rolls execute on press through the vanilla `ExecEvasion` roll path. That keeps FromSoftware's normal `Rolling` / `Rolling_Selftrans` behavior instead of using a synthetic roll event that would mess up recovery and roll queueing (shoutout to f_wang for the roll on press code).

Release-triggered `ACTION_ARM_ROLLING` is suppressed after a press roll so hold-to-roll does not fire a second roll on button release.

Sprint is removed from the original dodge hold path in `SpeedUpdate()` and is driven by `separate_roll_and_sprint.dll`, which exposes native HKS helpers backed by `separate_roll_and_sprint.ini`. Keyboard dash is sprint-only. Controller required a bit more work: dash can sprint while the stick is tilted; neutral controller dash toggles crouch/stealth with HKS lockout handling.

## Build (native DLL)

Requires Visual Studio / MSVC (or clang-cl with the VS STL).

```powershell
cmake -S native -B native/build
cmake --build native/build --config Release
```

Release output:

- `native/build/Release/separate_roll_and_sprint.dll`
- `native/build/Release/separate_roll_and_sprint.ini` (copied post-build)

In-game: **F10** reloads the INI. Log file: `separate_roll_and_sprint.log` (next to the DLL).

Legacy INI name `ERKeyAssignDashInputBridge.ini` is still read if `separate_roll_and_sprint.ini` is missing.

**HKS sync:** Edit `bind/mod/action/script/c0000.hks`, then copy to root `c0000.hks` before committing or deploying.

## INI Example (keyboard + controller)

```ini
; separate_roll_and_sprint — unified keyboard + controller dash config.

dash_key=LeftShift
dash_button=L3
dash_trigger_threshold=80
gamepad_index=any
left_stick_dash_deadzone=12000

enable_menu_patch=0

; dash_key (keyboard): sprint only — never toggles crouch.
; dash_button (controller): neutral stick = crouch/stealth, tilted stick = sprint.
; F10 in-game: re-read this INI
```

### INI keys

| Key | Description |
|-----|-------------|
| `dash_key` | Optional keyboard (`LeftShift`, `Space`, `F`, `A`–`Z`, …). **Sprint only** — never toggles crouch. |
| `dash_button` | Optional controller (`L1`/`LB`, `L3`/`LS`, `Cross`/`A`, `L2`, `R2`, D-pad, …). |
| `dash_trigger_threshold` | `0`–`255` for analog `L2`/`R2` (default `80`). |
| `gamepad_index` | `any` (players 0–3) or `0`–`3`. |
| `left_stick_dash_deadzone` | Stick magnitude below this = neutral (crouch path on controller). |

### Roll direction (after-attack)

Movement keys default to **WASD** for after-attack roll direction sampling. Arrow keys are also supported internally. You do not need to configure movement keys in the INI for normal use.

Advanced users can add `move_forward`, `move_back`, `move_left`, and `move_right` manually if they use custom keyboard movement binds (see the commented advanced section in `separate_roll_and_sprint.ini`).

Controller movement is read from the **XInput left stick** and does not require INI movement keys. Optional `movement_stick_deadzone` (default `12000`) is documented in that advanced section for controller tuning.

**Future research (not required for release):** discover where Elden Ring stores live keyboard movement bindings so the DLL could read in-game binds automatically. Until then, WASD defaults with optional manual override are sufficient.

**Controller `dash_button` behavior:** Native code stick-gates sprint. Button held + neutral stick -> HKS crouch/stealth toggle (with lockout/rearm). Button held + tilted stick -> sprint. After controller sprint release, HKS applies a short reactivation cooldown (`ERSPLIT_CONTROLLER_SPRINT_COOLDOWN_FRAMES` in `c0000.hks`, default 6); keyboard sprint is unaffected. Avoid binding the same physical button elsewhere in-game.

### Controller note (XInput)

Windows exposes controllers through **XInput** (Xbox-style buttons). PlayStation names in the INI are **aliases** (`L1` = `LB`, `Cross` = `A`, etc.). DualSense / DualShock need Steam Input, DS4Windows, or another XInput layer.

XInput loads dynamically (`xinput1_4.dll` -> `xinput1_3.dll` -> `xinput9_1_0.dll`). If unavailable, keyboard `dash_key` still works.

## Repo Layout

| Path | Role |
|------|------|
| `bind/` | Ready-to-copy ME3 deploy package |
| `native/` | `separate_roll_and_sprint` C++ source (CMake) |
| `c0000.hks` | Root copy of player script (keep in sync with `bind/mod/...`) |
| `libER/` | Optional vendored experiments (not required to ship) |

## Manual Test Checklist

- Moving tap dodge: roll on press (vanilla animation).
- Moving hold dodge: no sprint from dodge hold.
- Release after press roll: no second roll (`SuppressReleaseRoll` in trace if logging enabled).
- Fast double tap: vanilla queued roll / self-trans, not animation restart spam.
- Standing tap: backstep (SP_MOVE roll gated by `MoveSpeedLevel`).
- Keyboard dash hold: sprint only.
- Controller dash neutral: crouch/stealth; tilted stick: sprint.
