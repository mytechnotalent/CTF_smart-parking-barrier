# smart-parking-barrier - Design Blueprint (Act IX, IRON FANG)

Repo: `smart-parking-barrier`
Companion CTF repo: `CTF_smart-parking-barrier` (artifact prefix `ACT-IX`)
Codename: IRON FANG
Author: Kevin Thomas (kevin@mytechnotalent.com)

## Act IX of the OPERATION COLD IRON saga

Act I the lie. Act II the door. Act III the payload. Act IV the payload that
would not die. Act V the payload that spreads. Act VI the payload that steals.
Act VII the payload that takes orders. Act VIII the payload that holds the
building hostage. Act IX is the payload that becomes a weapon.

The smart parking barrier is a boom gate. FROSTLINE's implant weaponizes the
actuator: it ignores the safety loop and slams the boom on command, turning a
gate into a physical hazard. WHITEOUT must disarm the weapon and restore the
safety interlock. This is the physical-weaponization lesson.

## Safety contract

- No network, no internet, no host impact. Bare-metal RP2350, no OS.
- The "weapon" is a servo boom on your own breadboard. No real hazard.
- Synthetic data only. No external address.
- A `SANDBOX_ONLY` build guard disables the implant.
- Every act ends in analysis and neutralization.

## Parity contract

Same layout, crypto, tooling, pin map, README standard, disclaimer, and REAL
flash persistence (0x103FF000) as Acts I-VIII.

## Pin map (identical, new roles)

| Pin | Act IX role |
| --- | ----------- |
| DHT11 GP4 | barrier cabinet temperature |
| LCD SDA GP2 / SCL GP3 | barrier status |
| IR GP5 | NEC monthly-pass remote |
| Servo GP14 | boom barrier |
| Red GP16 | DENIED |
| Yellow GP17 | PASS PENDING |
| Green GP18 | OPEN |
| Button GP15 | manual raise |
| RYLR998 GP8/9 | parking link |
| Debug Probe | weaponization analysis |
| Onboard GP25 | heartbeat |

## Fix track

- Pass/raise commands must be sealed and authorized.
- Manual raise must not silently bypass authorization.
- The barrier must honor the safety interlock and fail safe.

## Malware track (physical weaponization, benign)

Module `include/implant.h` + `src/implant.c`, only under `SANDBOX_ONLY`:

- **Weaponize.** Force the boom down (slam) on a magic command, ignoring the
  safety interlock.
- **Ignore safety.** Mask the safety loop so the barrier never yields.
- **Weapon marker.** Program a weapon marker into the reserved flash sector
  (`BARRIER_IMPLANT_RESERVE_ADDR` 0x103FF000) with the real flash API.
- **Anti-debug.** Reads DHCSR and behaves benignly under a probe.
- **Neutralization.** Disarm the weapon, restore the interlock, clear the marker.

## Companion CTF: ACT-IX, four deep tasks

| Task | Points | Objective |
| ---- | ------ | --------- |
| 1 | 10 | Setup and analysis |
| 2 | 20 | Disarm the boom slam |
| 3 | 20 | Restore the safety interlock |
| 4 | 20 | Clear the weapon marker |
| 5 | 20 | Seal the barrier command path (fix track) |
| 6 | 10 | Export, verify, hardware proof, reflection |

Every patch is in-place and same-size.

## Naming

Project `smart-parking-barrier`; companion `CTF_smart-parking-barrier`;
prefix `ACT-IX`.
