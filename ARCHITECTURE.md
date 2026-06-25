# Architecture: MSP430 + nRF52 Bonito Firmware

## Components

```
  ┌──────────────┐   C2C SPI    ┌──────────────┐   BLE adverts   ┌────────────┐
  │  MSP430FR    │ ───────────► │  nRF52833    │ ──────────────► │  scan.py   │
  │  5962        │              │  (Riotee)    │                 │  (laptop)  │
  └──────────────┘              └──────────────┘                 └────────────┘
       FRAM                          RAM                          terminal
  (counter lives here)        (Bonito model lives here)
```

| Component | Role |
|-----------|------|
| **MSP430** | SPI master. Maintains a FRAM-backed counter. Sends the latest counter to the nRF52 every ~100–200 ms and waits for `ACCEPTED`. |
| **nRF52** | SPI slave + BLE radio. Runs the Bonito scheduling loop. Accepts payloads from the MSP430 and broadcasts them at Bonito-computed intervals. |
| **AM1805 RTC** | Real-time clock on the nRF52 side, powered by the same rail as VCC (no separate battery — a large capacitor on VBAT bridges brief outages). Used in `real` mode to set a wake-up alarm before the nRF52 powers down. |
| **scan.py** | Laptop BLE monitor. Filters by the node's MAC address and Nordic manufacturer ID. Decodes each advertisement and displays the sequence number, CI, and counter. |

---

## Data Flow

### 1. MSP430 → nRF52 (C2C SPI)

The MSP430 is the SPI master; the nRF52 is the slave (SPIS2).

```
MSP430 sends [CMD=SET_PAYLOAD | LEN=4 | counter (4 bytes) | pad]
nRF52  responds with its current status byte on the next transaction
MSP430 polls GET_STATUS every ~100 ms until it sees ACCEPTED
```

The nRF52 latches the payload immediately in the SPI ISR (latest-wins: a new
payload overwrites the previous one if the Bonito loop hasn't consumed it yet).
It replies `ACCEPTED` on the next poll.  The Bonito loop consumes it
asynchronously — the MSP430 never waits for the radio.

### 2. nRF52 → Laptop (BLE advertisement)

Every Bonito round the nRF52 broadcasts three back-to-back non-connectable
advertisements (one per channel) carrying the node's raw model parameters
(per paper Fig. 12 — the CI is NOT on the wire; the peer computes it locally):

| Field | Size | Description |
|-------|------|-------------|
| `seq` | 2 B | Round index. Increments every round; resets to 0 on every nRF52 reboot. |
| `model_type` | 1 B | Distribution type (`0x01` = Normal). |
| `app_len` | 1 B | Payload byte count (currently 4 for the MSP430 counter). |
| `mean` | 4 B | Charging-time distribution mean (seconds, float32). |
| `var` | 4 B | Charging-time distribution variance (seconds², float32). |
| `app[]` | up to 4 B | Opaque application payload — today a 4-byte little-endian counter. |

scan.py filters on the device MAC address and Nordic manufacturer ID `0x0059`,
decodes `mean` and `var`, and recomputes CI locally as
`mean + sqrt(var) × Φ⁻¹(0.99)` (mirroring what the node computed).

---

## State Inventory

### MSP430

| Variable | Storage | Survives power cut? | Survives reflash? |
|----------|---------|---------------------|-------------------|
| `g_counter` | FRAM (`#pragma PERSISTENT`) | **Yes** — FRAM is non-volatile | **No** — linker resets the initial value to 0 |
| `g_initialized` | FRAM (`#pragma PERSISTENT`) | **Yes** | **No** — cleared by linker; triggers 30-blink first-boot sequence |
| `working` (in-flight counter) | RAM | **No** — lost on any reboot | N/A |

On boot the MSP430 sets `working = g_counter` (last committed value), then
immediately does `working++` before the first send.  If power is cut after
`ACCEPTED` but before the next `working++`, `g_counter` already holds the
committed value and the next boot re-sends `g_counter + 1`.  If power is cut
before `ACCEPTED`, the same `working` value is retried on the next boot.  The
counter never goes backward.

### nRF52

| Variable | Storage | Survives power cut? |
|----------|---------|---------------------|
| `seq` | RAM (stack in `main()`) | **No** — always 0 on boot |
| `dist` (mean, var, eta) | RAM (stack in `main()`) | **No** — re-initialized to priors on every boot |
| `g_new_payload`, `g_app_buf` | RAM (BSS) | **No** |
| `g_sleep_epoch`, `g_sleep_valid` | RAM (BSS, `rendezvous_real.c`) | **No** |

> **Why is there no nRF52 checkpoint?**
> The Riotee SDK uses SPIM0 (the nRF52's master SPI) to write to an external
> FRAM chip — and the C2C SPI pins are shared with that FRAM.  Our `spis_init()`
> disables SPIM0 and reconfigures those pins as SPIS2 (slave to the MSP430).
> As a result, every call to `riotee_checkpoint()` silently returns -1 without
> writing anything.  The call is currently left in the code but is a no-op.
> **Every nRF52 boot is a fresh start.**

---

## Bonito Protocol (what the nRF52 does each round)

Bonito (NSDI 2022) is an online scheduling protocol for intermittently-powered
nodes.  Each node maintains a Normal distribution of its own capacitor charging
times and uses the inverse CDF at a target probability (p = 0.99) as its
*connection interval* (CI) — the time to wait before the next transmission.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  On boot                                                                    │
│    dist ← Normal(mean=1.0 s, var=0.25 s², η=0.01)                          │
│    seq  ← 0                                                                 │
└───────────────────────────────┬─────────────────────────────────────────────┘
                                │
                     ┌──────────▼──────────────────────────────────────┐
                     │  Wait for C2C payload from MSP430               │
                     │  (enters low-power mode until g_new_payload)    │
                     └──────────┬──────────────────────────────────────┘
                                │
                     ┌──────────▼──────────────────────────────────────┐
                     │  c  ← charge_source_next()                      │
                     │       sim:  next value from CHARGE_TRACE[]      │
                     │       real: elapsed = now − g_sleep_epoch       │
                     │             (time since last rendezvous_wait)   │
                     └──────────┬──────────────────────────────────────┘
                                │
                     ┌──────────▼──────────────────────────────────────┐
                     │  ci ← bonito_connection_interval(              │
                     │           &dist, peer=NULL, p=0.99)            │
                     │       = dist.ppf(0.99)  (always-on peer §7)   │
                     │  ci clamped to [0.1 s, 30 s]                   │
                     └──────────┬──────────────────────────────────────┘
                                │
                     ┌──────────▼──────────────────────────────────────┐
                     │  Advertise {seq, model_type, app_len,          │
                     │             mean, var, app[]} × 3 bursts       │
                     │  (paper Fig. 12: model params, not CI)         │
                     └──────────┬──────────────────────────────────────┘
                                │
                     ┌──────────▼──────────────────────────────────────┐
                     │  dist.sgd_update(c)                             │
                     │  seq++                                          │
                     └──────────┬──────────────────────────────────────┘
                                │
                     ┌──────────▼──────────────────────────────────────┐
                     │  riotee_sleep_ms(200)   ← HACK (see KL #4)     │
                     │  Give MSP430 time to see ACCEPTED and commit    │
                     │  g_counter to FRAM before disable_power fires.  │
                     └──────────┬──────────────────────────────────────┘
                                │
                     ┌──────────▼──────────────────────────────────────┐
                     │  rendezvous_wait(ci)                            │
                     │    sim:  riotee_sleep_ms(ci × 1000)             │
                     │    real: set AM1805 alarm at (now + ci)         │
                     │          riotee_am1805_disable_power()          │
                     │          → falls through to sleep_ms on bench   │
                     └──────────┬──────────────────────────────────────┘
                                │
                     ┌──────────▼──────────────────────────────────────┐
                     │  Wait for next C2C payload  (loop back)         │
                     └─────────────────────────────────────────────────┘
```

**Always-on peer simplification (paper §7):** The laptop's charging-time CDF ≡ 1,
so the inverse joint CDF degenerates to the node's own `ppf(p)`.  `peer_dist = NULL`
in `bonito_connection_interval()` triggers this path.  For two intermittent nodes,
populate `peer_dist` from the received advertisement — the bisection path then
computes the true joint CI per paper §3.4.

**Model convergence:** With η = 0.01 the distribution converges in roughly 100
rounds (~100 s at a 1 s CI).  Starting from priors on every boot is therefore
acceptable — the model re-converges within ~2 minutes.

---

## Power Scenarios

### Scenario 1: Steady operation — bench PSU always on

```
PSU: ────────────────────────────────────────── (on)
Cap: ████████████████████████████████████████ (full)
```

| | Behavior |
|-|----------|
| **MSP430** | Counter increments every ~100–200 ms. Each new value is committed to FRAM after `ACCEPTED`. LED blinks off briefly with each acknowledgement. |
| **nRF52** | Bonito loop runs with `riotee_sleep_ms()` as the sleep mechanism. `riotee_am1805_disable_power()` returns immediately (cap never drains below the AM1805 threshold on bench power). `charge_source_next()` measures elapsed time since `g_sleep_epoch`, which is approximately equal to `ci_s` (the board slept for exactly that long). |
| **Bonito** | `seq` grows monotonically. `dist` converges over the first ~100 rounds then stabilizes. CI converges to roughly the observed charging times (~CI seconds in steady state). |
| **scan.py** | Shows `seq` and `counter` incrementing, CI stabilizing, trend arrow `→`. Session # stays fixed. |

### Scenario 2: Brief power interruption — PSU cut then restored within seconds

```
PSU: ─────────────────────┐     ┌────────────────── (off then on)
Cap: █████████████████████▓▓▓▓▓▒▒░░░░░░░░░░░░░░░░░ (drains on cap energy)
VBAT: same as cap — LED may blink for a few seconds after PSU cut
```

The large capacitor on VBAT keeps the board alive briefly.  The AM1805 RTC
continues running as long as VBAT (the cap) holds charge.  If PSU is restored
before the cap is fully drained:

| | What is preserved | What is lost |
|-|------------------|--------------|
| **MSP430** | `g_counter` in FRAM (last committed value). `g_initialized` magic. | `working` in RAM — the in-flight increment. On next boot the same or next counter value is re-sent. |
| **nRF52** | Nothing — `seq` and `dist` are RAM-only. | Everything. Boot is fresh: `seq=0`, `dist` reset to priors. |
| **AM1805 RTC** | Time (if cap held charge). Alarm setting (if it was set). | — |
| **scan.py** | — | Detects backward seq jump → logs "node rebooted", starts a new session, resets its reference Bonito model. |

> Even if the AM1805 alarm fires at the scheduled time and wakes the board, the
> nRF52 still boots fresh (seq=0, dist reset) because the checkpoint mechanism
> does not work.  The AM1805 alarm is effectively irrelevant until checkpointing
> is fixed (see Known Limitations).

### Scenario 3: Full power loss — PSU off long enough for cap to drain

```
PSU: ─────────────────────┐                    ┌────────────────── (long outage)
Cap: █████████████████████▓▓▓▓░░░░░░░░░░░░ ░ ░  (drains fully)
VBAT: dies → AM1805 RTC loses time
```

| | Behavior |
|-|----------|
| **MSP430** | Same as Scenario 2 for counter. `g_counter` restored from FRAM on boot. 3 rapid blinks. |
| **nRF52** | Fresh boot: `seq=0`, `dist` reset to priors. |
| **AM1805** | RTC time lost. Any pending alarm is gone. On next `lateinit()`, `riotee_am1805_init()` re-initializes the chip (verifies ID, blocks until chip is ready). |
| **scan.py** | Sees `seq` reset to 0, logs "node rebooted", new session. Counter jumps to wherever the MSP430 left off (e.g., `counter=847`). |

This is the "complete power loss → re-find" case.  The counter is the only state
that survives.  Bonito re-converges within ~2 minutes.

### Scenario 4: MSP430 first boot after programming

```
g_initialized ≠ 0xBEEF → g_counter = 0, g_initialized = 0xBEEF, blink 30×
```

Counter starts at 1 (first `working++` after boot sets it to 0+1 = 1).
scan.py will see `counter=1` on the first received advertisement.

### Scenario 5: nRF52 unresponsive (sleeping or crashed)

The MSP430 sends `SET_PAYLOAD` and polls `GET_STATUS` every 100 ms.  If no
`ACCEPTED` arrives within 30 polls (~3 s), it blinks 3 rapid times and retries
the same counter value.  It retries indefinitely.

Most commonly this is benign: the nRF52 is in `riotee_sleep_ms()` for the CI
duration and its SPI ISR is inactive.  The MSP430 will eventually get `ACCEPTED`
when the nRF52 wakes.  The nRF52 latches the *latest* payload (latest-wins), so
rapid retries from the MSP430 during the nRF52 sleep are harmless — only the
last one matters.

| Timeout cause | Outcome |
|---------------|---------|
| nRF52 sleeping during CI | MSP430 retries; nRF52 wakes, accepts, loop continues. Counter value is the same across retries — no gap in counter. |
| nRF52 crashed / not yet booted | MSP430 retries indefinitely (3 blinks each cycle). Once nRF52 boots it replies ACCEPTED and normal operation resumes. |
| nRF52 never responds | MSP430 loops forever with 3-blink retry cadence. LED stays in blink pattern. |

### Scenario 6: Real energy harvesting (future — not fully tested)

When operating from a solar panel or RF harvester instead of a bench PSU:

1. Cap charges until `PWRGD_H` → board boots.
2. nRF52 runs the Bonito round (fresh start: seq=0, dist = priors).
3. `rendezvous_wait(ci)` calls `riotee_am1805_set_alarm(now + ci)` then
   `riotee_am1805_disable_power()`.
4. Since the harvester cannot sustain the board (cap drains), the AM1805
   actually cuts the power rail when the cap drops below its threshold.
5. Board is fully off. AM1805 runs on VBAT (the cap still has residual charge
   from the trickle-charged VBAT rail — or a coin cell if added).
6. At `now + ci` the AM1805 alarm fires and re-enables the power rail.
7. Cap charges again (step 1). Nrf52 boots fresh — same as today.

The difference from bench PSU: steps 4–6 actually happen, and
`charge_source_next()` correctly measures the true capacitor recharge time
(elapsed from `g_sleep_epoch` to boot) rather than the sleep duration.

> Until the checkpoint mechanism is repaired (see Known Limitations), every
> boot after harvesting is still a fresh start.  This is acceptable given the
> fast convergence of the Bonito model.

---

## scan.py Session Model

scan.py tracks sessions (separated by `seq` jumping backward) and rounds
(advertisements within a session).

```
[FOUND] Riotee node responding  rssi=-54 dBm  (resuming from checkpoint, seq=N)
seq=    0  ci=  2320 ms ↑ [real]  loss= 0.0%  counter=847  rssi=-54 dBm
       session #3 round    1  next TX ~14:22:01 (+2320 ms)  node µ=1.000s σ=0.500s ...
seq=    1  ci=  2295 ms ↓ [real]  loss= 0.0%  counter=848  rssi=-54 dBm
...
  ** node rebooted  seq 72→0  session #3: 72 rounds in 88s  loss=0.0%  ci 2320→1890 ms
```

- **`seq`** is the nRF52's round index — resets on every nRF52 reboot.
- **`counter`** is the MSP430's FRAM-backed value — persists across reboots.
- **CI trend arrow** (↑↓→) shows whether the model's predicted CI is growing,
  shrinking, or stable.
- **`[real]`** / **`[ok]`** / **`[MISMATCH]`** — in `real` mode CI verification
  is skipped (no shared trace); in `sim` mode the laptop replays the same
  deterministic trace and checks the node's CI matches.

---

## Known Limitations

### 1. `riotee_checkpoint()` is a no-op

`spis_init()` disables SPIM0 and reconfigures the C2C SPI pins as SPIS2 (slave
to the MSP430).  The Riotee SDK's NVM driver also uses SPIM0 on those same pins
to reach the external FRAM.  After `spis_init()` runs, `checkpoint_store()` fails
silently on every call because `is_ready()` (checking the FRAM-ready GPIO) returns
-1.

**Consequence:** Every nRF52 boot is a fresh start.  `seq` and `dist` are
always reset, regardless of whether the prior shutdown was clean (AM1805 alarm)
or not (power cut).

**Future fix options:**
- Wire the FRAM to a different SPI bus (hardware change).
- Use the AM1805's 192-byte battery-backed SRAM (I2C, separate from SPI) to
  persist a compact snapshot of `dist` and `seq`.  At ~20 bytes this fits easily
  and survives as long as the VBAT cap holds charge.

### 2. `riotee_am1805_disable_power()` does not cut power on bench PSU

With a bench supply at 40 mA the cap stays above the AM1805 sleep threshold.
`riotee_am1805_disable_power()` writes the SLST bit and returns 0 immediately
without blocking.  The board falls through to `riotee_sleep_ms()` and the AM1805
alarm is effectively unused.

**Consequence:** In real mode on bench PSU, the firmware behaves identically to
sim mode for the sleep phase.  The only real-mode difference is that
`charge_source_next()` measures elapsed wall-clock time (via the RTC) rather than
replaying a fixed trace.

**Not a bug in production:** With a real harvester, the cap drains below the
threshold and AM1805 actually cuts the rail.

### 4. 200 ms post-advertising sleep to avoid ACCEPTED commit race (hack)

After the three advertising bursts the nRF52 sleeps 200 ms before calling
`rendezvous_wait()` / `disable_power()`.

**Why it is needed:** The nRF52 completes a Bonito round (boot overhead + C2C
handoff + BLE burst) in roughly 20–60 ms.  The MSP430 polls `GET_STATUS` every
~100 ms.  Without this delay, `disable_power` can cut the supply before the
MSP430's poll fires, so the MSP430 never sees `ACCEPTED` and never commits
`g_counter` to FRAM — leaving the counter stuck at the last committed value.

**Why 200 ms is safe:** During this window the nRF52 radio is off and the chip
draws ~5.6 mA idle — well under the 30 mA PSU limit.  The capacitor charges
rather than drains.  200 ms guarantees at least one full MSP430 poll cycle
regardless of phase alignment.

**Why this is a hack:** The correct fix is to make the MSP430 send an explicit
"ready to sleep" signal so the nRF52 only calls `disable_power` after the MSP430
has confirmed its commit, rather than waiting a fixed duration.  A new C2C command
(`C2C_CMD_COMMIT_DONE`) or a dedicated handshake GPIO would do this robustly.

**Observable side-effect:** Because the MSP430 loops fast, it sends a second
(and sometimes third) counter value to the nRF52's latest-wins buffer during the
200 ms window.  The nRF52 does not advertise these extra values in the current
round — they are picked up on the next boot.  This causes the advertised counter
to jump by **2–3 per power cycle** rather than 1 (see scan.py output above).
This is correct behavior: no counter values are skipped or lost, they are simply
committed to FRAM faster than the nRF52 advertises them.

---

### 5. VBAT = VCC (no dedicated battery)

The AM1805 VBAT is tied to the main power rail, bridged by the capacitor on the
board.  The RTC (and any AM1805 SRAM contents) survive only as long as the cap
holds charge after a power cut — typically a few seconds at idle.

**Consequence:** Power cuts longer than the cap discharge time lose the RTC time
and any SRAM contents.  This is treated as "complete power loss → fresh start",
which is the correct behavior.

---

## Deviations from the Original Bonito Paper

This section documents where our implementation differs from the full Bonito
design (NSDI 2022), with the reason for each.

Key to labels:

- **(always-on)** — intentional for the always-on laptop special case; resolves
  automatically when both peers are intermittent.
- **(structural)** — requires a hardware or architectural change to fix; not just
  a software decision.
- **(out of scope)** — deliberate omission to keep scope small; easy to add later.

---

### 1. TX-only radio — node cannot receive the peer's model (structural)

**What the paper expects:** Both nodes broadcast their model parameters
`(model_type, mean, var)` during each encounter and independently compute the same
joint CI from both models.  The encounter is bidirectional.

**What we do:** The nRF52 radio is configured as a non-connectable advertiser only
(`TASKS_TXEN`, `ADV_NONCONN_IND` in `Riotee_SDK/core/ble.c`).  The node transmits
its model but never receives the peer's.  `peer_dist` in
`bonito_connection_interval()` is always `NULL` at runtime; the bisection path is
implemented and correct but is never exercised.

**Why:** The Riotee SDK's `ble.c` only exposes the advertiser role.  The nRF52833
hardware is fully capable of scanning and receiving, but enabling it requires
restructuring `ble.c` to add a scanner state machine, managing RX buffers, and
handling receive interrupts — significantly more complexity.  For the always-on
laptop special case this is unnecessary: the laptop receives via the host BLE
adapter and `scan.py`; the node only needs to transmit.

**Path to fix:** Add scanner role to `ble.c` (`TASKS_RXEN`, `SCANNER` state, RX
buffer + interrupt handler).  On each received advertisement, parse the peer's
`(model_type, mean, var)` and store them in `peer_dist` in `main.c`.  The rest of
the code already handles this — `bonito_connection_interval(&dist, &peer_dist, p)`
with a non-NULL peer activates the joint-CDF bisection path.

---

### 2. No Find protocol (always-on)

**What the paper expects:** Paper §4 describes a duty-cycled Find phase that two
intermittent nodes use to discover each other before any schedule exists.  Neither
node knows when the other will wake up, so they scan and advertise probabilistically
until they collide.

**What we do:** There is no Find phase.  The node starts advertising from the first
Bonito round and the laptop hears it immediately.

**Why:** The laptop scans continuously (`SCAN_INTERVAL_MS = SCAN_WINDOW_MS = 10 ms`,
100% duty cycle) and is always awake.  The Find problem — two intermittent nodes
not knowing each other's schedule — simply does not exist when one peer is
always-on.  The node's very first advertisement is its "find" packet.

**Path to fix:** When both peers are intermittent, implement the Find protocol
from §4 as an initial state before the Bonito loop.  The probability parameters
from the paper can be computed from the energy budget and acceptable discovery
latency.

---

### 3. Only Normal distribution — Exponential and GMM not implemented (out of scope)

**What the paper expects:** Three model types: Normal (unimodal symmetric charging),
Exponential (Poisson-like harvesting events), and Gaussian Mixture Model (GMM) for
multimodal environments (e.g., a device that alternates between indoor and outdoor
light levels).  The model type is chosen per node based on its harvesting profile.

**What we do:** Only Normal is implemented in `bonito_dist.c`.  The `model_type`
byte in the advertisement (`BONITO_MODEL_NORMAL = 0x01`) reserves the field for
future types, and `scan.py` rejects unknown `model_type` values with a warning.

**Why:** Normal covers the charging behaviour of a capacitor under a roughly
constant harvesting source, which is the target scenario.  Exponential and GMM
add code complexity (GMM in particular requires EM-fitted parameters and a
numerical `ppf`) for scenarios we are not yet testing.  The wire format and
`model_type` check are already in place; adding a new type only requires new
`cdf`/`ppf` implementations in `bonito_dist.c` and a corresponding decode in
`scan.py`.

---

### 4. Model not persisted — always restarts from priors (structural)

**What the paper expects:** The model (`mean`, `var`) is a long-lived estimate that
improves over many encounters across multiple power cycles.  A node with weeks of
data has a far more accurate charging-time distribution than one that just booted.

**What we do:** `riotee_checkpoint()` is a no-op (see Known Limitation #1), so
every nRF52 boot discards `dist` and re-initializes to `mean=1.0s, var=0.25s²`.
The model re-converges in ~100 rounds (~2 min at a 1 s CI), but the long-run
accuracy the paper describes is never reached.

**Why:** The Riotee SDK writes checkpoints via SPIM0 to an external FRAM on pins
`PIN_C2C_CLK/MOSI/MISO/CS`.  Our `spis_init()` disables SPIM0 and reclaims those
same pins as SPIS2 (the SPI slave to the MSP430).  The two functions share exactly
the same physical wires and cannot coexist.  The checkpoint call therefore silently
fails every time — `is_ready()` sees the FRAM-ready GPIO in the wrong state and
`checkpoint_store()` returns -1 without writing.

**Path to fix:** Two options without a hardware respin:

1. Use the AM1805's 192-byte battery-backed I²C SRAM (registers `0x40`–`0xFF`)
   which is on a separate bus (`PIN_SYS_SCL/SDA`) unaffected by SPIS2.
   A compact `dist` snapshot (`mean 4B + var 4B + seq 2B = 10B`) fits with room
   to spare and survives as long as the VBAT capacitor holds charge.

2. Pass the `dist` snapshot to the MSP430 via the C2C protocol (add a new command
   `C2C_CMD_SAVE_MODEL`) and store it in the MSP430's FRAM alongside the counter.
   On boot the nRF52 requests `C2C_CMD_LOAD_MODEL` before entering the loop.

---

### 5. Charging time observation is self-referential on bench PSU (structural)

**What the paper expects:** Each round, the node measures the actual time it took
the capacitor to charge from empty to the threshold — a physical observation
independent of the CI it predicted.  The SGD then corrects the model toward the
true charging distribution.

**What we do:** `charge_source_real.c` measures `c = now − g_sleep_epoch`, the
wall-clock time elapsed since `rendezvous_wait()` was called.

**Why this is wrong on bench PSU:** The board never actually powers off on bench
power, so `c ≈ CI` — the board simply slept for exactly `CI` seconds and then
measured how long it had been asleep.  The SGD is trained on approximately the
value it is trying to predict, so the distribution drifts toward whatever CI
converges to rather than toward the true charging time.

**Why `charge_source_real.c` is still correct for real harvesting:** With a real
harvester the board is fully off during `rendezvous_wait()`.  The elapsed time
from `g_sleep_epoch` to the next boot is then the true capacitor charging time —
the time from when the cap started charging (after the previous round's energy was
spent) to when it crossed the `PWRGD_H` threshold.  `charge_source_real.c` needs
no change; the measurement is only meaningless because the bench PSU keeps the
board alive.

---

### 6. Success rate and relative delay metrics not tracked (out of scope)

**What the paper expects:** Two performance metrics are tracked per-session:

- **Success rate** = fraction of rounds where `c ≤ CI`.  Should converge to the
  target probability p = 0.99 as the model learns.
- **Relative delay** = `median(CI) / median(T*_C)` where `T*_C = max(c_i, c_j)`.
  Measures how close CI is to the theoretical optimum.

**What we do:** Neither is computed.  The `c <= ci` boolean is available between
steps (b) and (c) of `main.c`; it is simply unused.

**Why:** These are evaluation metrics, not protocol correctness requirements.  The
CI trend (`↑↓→` in `scan.py`) gives a qualitative view of convergence.  Tracking
proper metrics requires additional payload bytes (or a UART) to get the data off
the node and is left for when we have real harvesting observations to evaluate.

---

### 7. CI clamp [0.1 s, 30 s] is our addition (out of scope)

**What the paper expects:** The model's `ppf(p)` is used directly as the CI.

**What we do:**

```c
if (ci < 0.1f) ci = 0.1f;
if (ci > 30.0f) ci = 30.0f;
```

**Why:** During the very first round the model has only its prior
(`mean=1.0s, var=0.25s²`), giving `CI ≈ 2.16 s`.  If the prior were poorly chosen
(e.g., very large `var`) the model could produce an absurdly long CI on the first
few rounds and leave the node sleeping for hours.  The clamp prevents this while
the model converges.  100 ms is the shortest interval that leaves enough energy
for a BLE burst; 30 s is an arbitrary upper safety bound.  The paper presumably
runs with a well-chosen prior; our hardware restarts from scratch on every boot
where the prior matters more.

---

### 8. Schedule jitter from C2C handoff (structural)

**What the paper expects:** Each round starts exactly `CI` seconds after the
previous encounter — both nodes wake at the same moment.

**What we do:** The nRF52 blocks on `while (!g_new_payload)` (waiting for the
MSP430 to hand over its payload) before computing `CI` and advertising.  The
actual advertisement is delayed by however long that wait takes — typically
tens to a few hundred milliseconds depending on the MSP430's polling cadence.

**Why:** Decoupling the Bonito schedule from the MSP430's payload cadence is
intentional (the nRF52 should control timing, not the MSP430).  But the current
design uses a single blocking wait rather than caching the last known payload and
starting the Bonito round immediately on wakeup.

**Impact:** For the always-on laptop this is invisible — the laptop is always
listening.  For two intermittent nodes it shifts the advertisement out of the
ideal rendezvous window by the handoff latency.

**Path to fix:** Start the Bonito round immediately after waking, using whatever
`g_app_buf` was last received.  The MSP430 can update the payload at any time and
it will be picked up on the next round.  This matches the "latest-wins" philosophy
already in place for the ISR.

---

### 9. Three advertisement bursts per round (our addition, out of scope)

**What the paper expects:** One packet exchanged per encounter.

**What we do:** `ADV_CH_ALL × 3 = 9 packets` per round.

**Why:** BLE advertising channels (37, 38, 39) each operate at a different
frequency.  A single burst can be wiped out by a competing 2.4 GHz device (Wi-Fi,
other BLE).  Three back-to-back bursts across all channels give 9 independent
transmission attempts, costing only ~4.5 ms of radio time per round — negligible
compared to a CI of 1–30 s.  For a real two-node scenario the peer would need to
be listening for at least the full 4.5 ms window at the scheduled CI time.
