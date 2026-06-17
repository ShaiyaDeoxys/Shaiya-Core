# Custom Resolutions (removed feature)

> **Status:** Removed from the client codebase. This document preserves the
> design and the runtime addresses so the feature can be reintroduced later if
> needed. The implementation previously lived in `sdev-client/src/resolutions.cpp`
> and was installed from `Main()` via `hook::resolutions()`.

## What it did

Extended the stock resolution table (indices `0..13`) with seven extra entries
so they appear in the in-game options screen and apply correctly:

| Index | Resolution |
|-------|------------|
| 14    | 1366x768   |
| 15    | 1400x900   |
| 16    | 2560x1080  |
| 17    | 2560x1440  |
| 18    | 3840x1080  |
| 19    | 3840x2160  |
| 20    | 3440x1440  |

Beyond the table itself, the feature also:

- Expanded option-screen resolution rendering, selection, saving, and apply.
- Reused the stock large-layout UI positioning branches so element placement
  stayed stable at the wider resolutions.
- Cached gamma / view-distance / float option values so expanding the resolution
  table did not corrupt the option sliders.

## Runtime addresses

Global state:

| Symbol                            | Address       |
|-----------------------------------|---------------|
| Resolution count                  | `0x007ADEB4`  |
| Saved resolution index            | `0x007C0DFC`  |
| Runtime resolution index          | `0x022B0224`  |
| Runtime width                     | `0x007AB0E8`  |
| Runtime height                    | `0x007AB0EC`  |
| Resolution table (index 14 entry) | `0x022B03D8`  |
| INI file path                     | `0x007C0720`  |
| INI `[Video]` section             | `0x00746E38`  |
| INI `SizeX` key                   | `0x00746E30`  |
| INI `SizeY` key                   | `0x00746E28`  |

`ResolutionEntry` layout (`sizeof == 0x18`):

```cpp
struct ResolutionEntry
{
    uint32_t id;        // resolution index
    char     text[20];  // e.g. "1366x768"
};
```

The new count was `21` (stock `0..13` plus new `14..20`), written to the count
address and reinforced by the `alter_limit_*` patches.

## Detours installed by `hook::resolutions()`

| Target      | Hook                       | Len | Purpose |
|-------------|----------------------------|-----|---------|
| `0x406591`  | `save_res_index`           | 10  | Map size → extra index when saving to INI |
| `0x51AEFC`  | `render_option_res_login`  | 5   | Select extra index on the options screen |
| `0x51AED6`  | `render_option_res_login2` | 5   | Second selection path |
| `0x51E864`  | `render_new_res`           | 5   | Reinstall table while enumerating |
| `0x51E849`  | `alter_limit_res`          | 6   | Force count to 21 |
| `0x51D738`  | `alter_limit_ok`           | 10  | Force count to 21 on OK |
| `0x51B30B`  | `set_new_res`              | 9   | Apply width/height + write INI for each new index |
| `0x54F770`  | `adjust_interface1`        | 9   | UI layout branch (login/options) |
| `0x54F1D1`  | `adjust_interface2`        | 9   | UI layout branch |
| `0x4954E1`  | `adjust_interface3`        | 9   | UI layout branch |
| `0x494D4D`  | `adjust_interface4`        | 5   | UI layout branch |
| `0x493447`  | `adjust_interface5`        | 9   | UI layout branch |
| `0x52159E`  | `cache_gamma_compare`      | 6   | Gamma slider cache |
| `0x5215B9`  | `cache_gamma_store`        | 5   | Gamma slider cache |
| `0x51B27C`  | `cache_gamma_load`         | 6   | Gamma slider cache |
| `0x521566`  | `cache_distance_compare`   | 6   | View-distance slider cache |
| `0x521581`  | `cache_distance_store`     | 5   | View-distance slider cache |
| `0x51B288`  | `cache_distance_load`      | 6   | View-distance slider cache |
| `0x5223B9`  | `cache_float_store1`       | 6   | Float option cache |
| `0x5224CD`  | `cache_float_load1`        | 6   | Float option cache |
| `0x5224E8`  | `cache_float_store2`       | 6   | Float option cache |
| `0x5224F0`  | `cache_float_compare`      | 6   | Float option cache |
| `0x522507`  | `cache_float_load2`        | 6   | Float option cache |

The interface-adjustment hooks branched on the resolution index:

- `0x0E` (14) → 1366-specific layout target
- `0x0F` (15) → 1400-specific layout target
- `0x10`+ (16+) → wide-layout target (3840 branch)
- `<= 0x0D` → original stock layout

## Reintroducing

Recreate `sdev-client/src/resolutions.cpp` with the table install +
`util::detour` calls above, re-declare `void resolutions();` in
`sdev-client/include/main.h` under `namespace hook`, and call
`hook::resolutions();` from `Main()` in `sdev-client/src/main.cpp`. Re-add the
`<ClCompile>` entry to `sdev-client.vcxproj` and `.filters`.
