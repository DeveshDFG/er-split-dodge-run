# ER Split Instant Dodge & Separate Sprint

Elden Ring Mod Engine 3 mod that separates dodge, sprint, and optional controller crouch behavior while keeping Elden Ring's vanilla roll animation path.

## Features

- Roll on press while moving.
- Vanilla roll queue / self-transition behavior is preserved.
- Release-triggered duplicate rolls are suppressed.
- Sprint can use a dedicated keyboard key or controller button.
- Optional mode: hold the dodge bind after the press-roll to sprint.
- Controller dash button can crouch/stealth when the left stick is neutral and sprint when the stick is tilted.
- After-attack roll direction uses native keyboard/controller input instead of stale `env(GetRollAngle)`.

## Files

Normal Mod Engine 3 deployment uses:

```text
bind/
  separate_roll_and_sprint.dll
  separate_roll_and_sprint.ini
  mod/action/script/c0000.hks
```

## Build

Requires Visual Studio / MSVC, or clang-cl with the Visual Studio STL.

```powershell
cmake -S native -B native/build
cmake --build native/build --config Release
```

Release output:

```text
native/build/Release/separate_roll_and_sprint.dll
native/build/Release/separate_roll_and_sprint.ini
```

## Install

Copy these into your ME3 profile's `bind` folder:

```text
separate_roll_and_sprint.dll
separate_roll_and_sprint.ini
mod/action/script/c0000.hks
```

Example native entry:

```toml
[[natives]]
path = "bind/separate_roll_and_sprint.dll"
```

The HKS file in `bind/mod/action/script/c0000.hks` is the deployed script. The root `c0000.hks` is kept as a synchronized reference copy.

## Configuration

Example `separate_roll_and_sprint.ini`:

```ini
dash_key=LeftShift
dash_button=L3
dash_trigger_threshold=80
gamepad_index=any
left_stick_dash_deadzone=12000
enable_menu_patch=0

; 1 = hold the dodge bind after roll-on-press to sprint.
; When enabled, dash_key/dash_button are ignored.
keep_sprint_on_dodge_hold=0
```

### INI keys

| Key | Description |
| --- | --- |
| `dash_key` | Optional keyboard sprint key, such as `LeftShift`, `Space`, `F`, or `A`-`Z`. |
| `dash_button` | Optional controller sprint/crouch button, such as `L3`, `L1`, `Cross`, `Circle`, `DPadUp`, `L2`, or `R2`. |
| `dash_trigger_threshold` | Analog trigger threshold for `L2` / `R2`, from `0` to `255`. Default: `80`. |
| `gamepad_index` | `any`, or a specific XInput pad index from `0` to `3`. |
| `left_stick_dash_deadzone` | Stick magnitude below this counts as neutral for controller crouch behavior. |
| `keep_sprint_on_dodge_hold` | `0`: sprint uses `dash_key` / `dash_button`. `1`: hold dodge after roll-on-press to sprint, and `dash_key` / `dash_button` are ignored. |
| `enable_menu_patch` | Unsupported diagnostic option. Keep `0` for normal play. |

In-game, press `F10` to reload the INI.

## Control Modes

### Separate Sprint Mode

```ini
keep_sprint_on_dodge_hold=0
```

- Moving dodge press: roll immediately.
- Holding the dodge bind: does not sprint.
- `dash_key`: sprint.
- `dash_button` with tilted stick: sprint.
- `dash_button` with neutral stick: crouch/stealth toggle.

### Dodge-Hold Sprint Mode

```ini
keep_sprint_on_dodge_hold=1
```

- Moving dodge press: roll immediately.
- Continue holding dodge: sprint after the roll.
- `dash_key` and `dash_button` are ignored.
- Controller neutral `dash_button` crouch is disabled in this mode.

## Controller Notes

Controller support uses XInput. Xbox button names and PlayStation aliases map to the same low-level inputs:

```text
A / Cross
B / Circle
X / Square
Y / Triangle
LB / L1
RB / R1
LT / L2
RT / R2
LS / L3 / LeftStick
RS / R3 / RightStick
Start / Options
Back / Share / Select
DPadUp / DPadDown / DPadLeft / DPadRight
```

DualSense and DualShock controllers need Steam Input, DS4Windows, or another XInput compatibility layer.

## Roll Direction

After-attack roll direction is sampled natively:

- Controller direction comes from the XInput left stick.
- Keyboard direction is read from the game's live movement binds through `GLOBAL_CSPcKeyConfig`.
- If live movement binds are unavailable, the DLL falls back to optional INI movement overrides and then WASD defaults.

Normal users do not need to configure movement keys in the INI.

## Manual Test Checklist

- Moving tap dodge: roll on press.
- Release after press-roll: no second roll.
- Fast double tap: vanilla queued roll / self-transition, not repeated animation restart.
- Standing tap dodge: backstep.
- After attack while locked on: roll direction follows the currently held movement input.
- `keep_sprint_on_dodge_hold=0`: dodge hold does not sprint; `dash_key` / `dash_button` sprint as configured.
- `keep_sprint_on_dodge_hold=1`: dodge hold sprints after rolling; `dash_key` / `dash_button` do nothing.
- Controller separate sprint mode: neutral `dash_button` toggles crouch; tilted `dash_button` sprints.

## Repo Layout

| Path | Role |
| --- | --- |
| `bind/` | Ready-to-copy Mod Engine 3 package |
| `bind/mod/action/script/c0000.hks` | Deployed player HKS |
| `native/` | C++ source for `separate_roll_and_sprint.dll` |
