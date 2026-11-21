# Audio: status as of 2026-11-21 (works)

## Current state (builds 0272-0284)
- iOS plays sound on the bottom speaker (build 0278; stutter fixed in 0279, no lost frames) and on Bluetooth A2DP headphones (0282).
- Keys: the D94 `product-id` is shown to audiomxd only (`IOUserClient.cpp`; backboardd aborts if everyone sees it), real-time scheduling floor lowered to 40 µs (`osfmk/kern/sched_rt.c`, 26 MHz timer), plug-in v2.
- Still to do: earpiece (rcv@41), microphone, amplifier protection firmware before raising the volume.

---

## History

The owner asked to stop there for audio, without a build. We will come back to it later.

### What works (on the phone, build 0213)
- **First beep heard**: at startup, the kernel plays a 1 kHz beep at −10 dB for 0.7 s on the bottom loudspeaker (CS35L40 bot@40), then cuts everything.
- Path used: the AP programs RDMA0 → SPUS → SIFS0 → UAIF1 of the ABOX itself, without the Calliope firmware. The audio buffer is in DRAM (ring_pa 0x84b8c000 in 0213).
- The two CS35L40 amplifiers (0x40 loudspeaker, 0x41 earpiece, id 35a40, rev. A0) answer on a simulated I2C bus by GPIO (gph0-6/7). They receive the rev. A0 errata and their factory OTP (cs35l41_otp_unpack), and the ASP is in dsp_a slave, 48 kHz.
- PLL_AUD0 locked at 1.18 GHz, ABOX v2.0.1, UAIF1 at 6.144 MHz (bclk).
- After the beep, everything is put back to its starting value, except PMU 0x1920 bit 11 (ABOX pin retention lifted). The amplifiers go back into reset.
- Wi-Fi was never disturbed.

### Steps and log (build by build)
- 0210 A0: read only. 0211 A1: amplifier identity. 0212 A2–A4: ABOX read, amplifiers set up, clocked by UAIF1. 0213 A5: the beep.
- Log: `grep -a "B2L AUDIO"` in `beyond2lte-ios/out/logs/021x-boot.txt`.
- Full technical details: `docs/audio/review-and-plan.md` §5 to §10.

### What is installed (in 0213)
- `iokit/Kernel/arm/B2LAudio.h` (sha 5112968d…), called by `b2l_audio_probe()` in `B2LCPU.cpp` just after `b2l_gpu_probe()`.
- Boot-arg `b2l_audio`: 0 off · 1 A0 · 2 +A1 · 3 +A2 · 4 +A3 · 5 +A4 · 6 (default) +beep.

### To check
- Register 0x4820 is 0x1 on bot and 0x100 on rcv. This is intended: bot is on channel 1 and rcv on channel 0, as at Samsung.
- ABOX v2.0.1: the RDMA position is read in bits 27:12.
- The meaning of bit 4 of RDMA CTRL0 is unknown.
- The ABOX SysMMU is assumed to be bypassed.
- No amplifier protection firmware (`cs35l40-*-spk-prot.wmfw`): keep the volume and duration low.

### Parked unfinished work (never built)
- The audio session was preparing the connection to iOS (batches 3–4: `/dev/b2l-audio`, then a driver seen by coreaudiod/audiomxd). Its unfinished `B2LAudio.h` is parked in `~/Developer/s10-port/audio-next-parked/B2LAudio.h.lot3-unfinished` (sha d76cae82…). It has not been re-read and may not compile. Only this file had changed in the overlay; `b2l_audio_seed.h` was never created.
- iOS-side audio plug-in (derived from HakiOS, MIT): source and `build.sh` in `Port iOS 26 to S10/audio/plugin/`, built in `~/Developer/s10-port/build-audio/b2l-audio-v1/`. Not on SYSTEM.
- `xnu-overlay/` is reset to the 0213 state for audio.

### Data
- `~/Developer/s10-port/vendor-audio/` (outside git): calliope*.bin, calliope2.dt, cs35l40-{bot,rcv}-dsp1-spk-prot.{wmfw,bin}, *-calib.bin (specific to this phone) and mixer_paths.xml. Copied read-only from vendor (erofs). Fingerprints in SHA256SUMS.

### To resume
1. Start again from 0213: the beep works.
2. Next planned batch: `/dev/b2l-audio` with a level guard, then the iOS side (AudioServerPlugIn or equivalent). If anything must go to USERDATA: the owner's agreement first.
3. Later: load the amplifier protection firmware before raising the volume.
