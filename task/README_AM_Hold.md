# AM Fix hold-time rescale — test build

## What this fixes

`hold_counter` used to be decremented once per pass through the active-AM
branch of `Task_AM_fix()`, and that branch only runs once every
`gAmFixCountdown` expiry (~50ms, confirmed 1ms system tick from TMR1 @
72MHz/÷72/period 999). So every `hold_time_short`/`hold_time_long` value in
the hysteresis profile table was actually counted in ~50ms units, not the
~3ms the tuning-guide docstring claimed.

Net effect: real hold times were running 15–50x longer than documented.
NARROW's `hold_time_long: 55` was ~2.75 seconds, not the ~240ms in the
comments. WIDE's was ~4.5 seconds.

It also meant the whole table only had 2–5 ticks of usable range (25–90),
so profiles were coarser than intended — a rounding difference of 1 tick is
a 20–50% swing in hold time.

## What changed

1. **`hold_counter` moved to the ISR.** Decremented once per 1ms system
   tick in `HandlerTMR1_BRK_OVF_TRG_HALL()` (scheduler.c), same pattern as
   `VOX_Timer` / `gIncomingTimer` already there. See `scheduler_patch.txt`.
2. **Profile table rescaled to real milliseconds** (`hold_time_short_ms` /
   `hold_time_long_ms`, widened to `uint16_t`) matching the original
   tuning-guide intent:

   | Profile  | short (ms) | long (ms) |
   |----------|-----------:|----------:|
   | NARROW   | 75         | 165       |
   | TIGHT    | 85         | 195       |
   | DEFAULT  | 90         | 240       |
   | RELAXED  | 120        | 255       |
   | WIDE     | 150        | 270       |

3. **Recovery-zone breathing buffer** (`hold_counter[vfo] = 15;`) rescaled
   to `750` (real ms) since it was also implicitly a ~50ms-unit value.
4. **`gAmFixCountdown` itself is untouched** — still 50 / 50 / 200 (all in
   real ms, confirmed via the 1ms TMR1 tick). It's now only responsible for
   loop timing, not hold-time timing.

## What did NOT change

- Hysteresis dB thresholds (`hysteresis_high_dB` / `hysteresis_low_dB`) —
  unchanged, they were already in real dB.
- EMA sample weighting — unchanged.
- The gain table, RSSI math, boundary protection logic — untouched, byte
  for byte copied from your current file.

## Testing checklist

- [ ] Confirm build with `hold_counter` now `volatile uint16_t` and shared
      between am-fix.c and scheduler.c doesn't trip any strict-aliasing or
      signedness warnings in your toolchain.
- [ ] Flash and run a known "pumping" scenario (e.g. NARROW profile, strong
      local AM overhead signal fading in/out) — hold behavior should now
      *feel* faster/tighter than before, since real hold times are much
      shorter than they were.
- [ ] Sanity-check WIDE/DX profile on a fading broadcast station — should
      still feel steady, not sluggish, at the new ~150/270ms real values.
- [ ] If any profile feels wrong once it's actually running at its
      documented speed, that's the moment to retune the ms values
      themselves — much easier to reason about now they mean what they say.

## Files in this directory

- `am-fix.c` — modified, ready to diff against your working copy
- `scheduler_patch.txt` — the ISR addition + am-fix.h extern needed
- `README.md` — this file
