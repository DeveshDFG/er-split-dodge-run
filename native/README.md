# separate_roll_and_sprint

Native ME3 bridge for split dodge (press-roll) and INI-driven sprint. **No `libER.dll` at runtime.**

## Build

```powershell
cmake -S native -B native/build
cmake --build native/build --config Release
```

Output (Release):

- `native/build/Release/separate_roll_and_sprint.dll`
- `native/build/Release/separate_roll_and_sprint.ini` (copied post-build)

Source INI: `native/separate_roll_and_sprint.ini`

Release builds run `dumpbin /DEPENDENTS` on the DLL; confirm `libER.dll` is not listed.

## Deploy (ME3 bind folder)

| File | Purpose |
|------|---------|
| `separate_roll_and_sprint.dll` | HKS native + dash key polling |
| `separate_roll_and_sprint.ini` | Preferred config (`dash_key=LeftShift`) |
| `bind/mod/.../c0000.hks` | Press-roll + sprint script |

Legacy config name `ERKeyAssignDashInputBridge.ini` is still read if the new INI is missing.

Log: `separate_roll_and_sprint.log` (next to the INI / ME3 bind folder).

Hotkey: **F10** reloads INI.

## INI

```ini
dash_key=LeftShift
enable_menu_patch=0
```

Supported `dash_key` names: `LeftShift`, `RightShift`, `Shift`, `Ctrl`, `Alt`, `Space`, `F`, `A`–`Z`, `0`–`9`.
