# 21 — Audio and media

Today: no sound at all (BETTER_OS_PLAN §4 wants virtio-sound + startup
chime + UI clicks). Target: a low-latency audio server with per-app
mixing, original codec implementations for common audio/video formats, a
media framework for players/browser, camera capture.

Rules: codecs are untrusted-input parsers → fuzzed and run in sandboxed
processes; decoders validated bit-exactly (or within spec tolerance)
against conformance streams where those are freely available; SIMD
(NEON/SSE) used in user space for performance; patent/licensing notes
recorded per codec in `THIRD_PARTY.md`/decision records.

---

## Audio

### AUD-01 — Kernel audio device interface
- Phase: P3 · Size: M · Status: todo
- Depends: DRV-CORE-01, MEM-11, SCH-04
- Files: kernel/drivers/audio/core.c

**Goal.** PCM device objects (playback/capture endpoints, supported
formats/rates/channels), ring buffers in shared memory mapped to the
audio server, period interrupts/timestamps (position + clock) for
drift-free mixing, jack/route events, hardware volume controls.
**Done when.** virtio-sound (DRV-VIRTIO-06) and HDA (DRV-AUD-01) expose
devices through it.
**Tests.** Host tests with a fake device; live tone test.
**Pitfalls.** Clock drift between devices.
**Refs.** —

### AUD-02 — Audio server (`audiod`)
- Phase: P3 · Size: L · Status: todo
- Depends: AUD-01, SCH-08, IPC-05
- Files: services/audiod/

**Goal.** Realtime mixing thread (float32), high-quality resampling
(polyphase/sinc), per-app and per-stream volume, system volume and mute,
output/input device selection and hot-switching (headphones plug →
move streams), latency targets (≤ 20 ms default, ≤ 10 ms low-latency
mode), soft clipping, level meters, ducking for notifications option.
**Done when.** Two apps play simultaneously with independent volumes;
glitch counter stays at 0 during a window-drag stress test.
**Tests.** Live tests reading back output (QEMU wav backend) and
comparing to expected mixes; host DSP unit tests.
**Pitfalls.** Never allocate or lock in the realtime thread.
**Refs.** —

### AUD-03 — Audio API for apps (`libaudio`)
- Phase: P3 · Size: M · Status: todo
- Depends: AUD-02
- Files: lib/libaudio/

**Goal.** Stream API (open, format, callback or push, latency hint),
simple "play file/sound" helpers, system sound IDs (DS-08), capture
API with microphone permission, device enumeration and change
notifications.
**Done when.** Sample player app and system sounds use it.
**Tests.** Live tests.
**Pitfalls.** —
**Refs.** —

### AUD-04 — Startup chime and UI sounds
- Phase: P3 · Size: S · Status: todo
- Depends: AUD-03, DS-08
- Files: services/audiod/, Settings > Sound

**Goal.** The ojjyOS startup chime at boot (toggleable), UI sounds
(Trash, screenshot, volume tick) — the first "wow" moment for audio.
**Done when.** Chime plays under VZ and QEMU on boot.
**Tests.** Live test (audio log line + captured output energy).
**Pitfalls.** Don't delay boot waiting for audio.
**Refs.** —

### AUD-05 — Audio effects: equalizer and loudness
- Phase: P6 · Size: S · Status: todo
- Depends: AUD-02
- Files: services/audiod/dsp/

**Goal.** System/per-app EQ presets, loudness normalization for Music
(ReplayGain/R128 metadata), mono audio and balance (accessibility).
**Done when.** Music EQ works without glitches.
**Tests.** Host DSP tests.
**Pitfalls.** —
**Refs.** EBU R128.

### AUD-06 — MIDI
- Phase: P7 · Size: M · Status: todo
- Depends: DRV-USB-02, AUD-03
- Files: kernel/drivers/usb/midi.c, lib/libmidi/

**Goal.** USB MIDI class devices, MIDI API (ports, routing, virtual
ports), a small built-in synthesizer (General MIDI with an openly
licensed SoundFont) for games/apps.
**Done when.** A USB keyboard plays notes through the synth (record).
**Tests.** Host tests of MIDI parsing.
**Pitfalls.** —
**Refs.** USB MIDI 1.0 spec.

---

## Media framework and codecs

### MED-01 — Media framework (`ojjyMedia`)
- Phase: P6 · Size: L · Status: todo
- Depends: AUD-03, GFX-03, MED-02
- Files: lib/libmedia/

**Goal.** Pipeline: source (file, HTTP range, HLS/DASH) → demuxer →
decoders (threads) → audio to libaudio, video frames to surfaces (zero
copy where possible) with A/V sync on the audio clock, seeking
(keyframe-accurate then exact), buffering states, playback rate,
tracks (audio languages, subtitles), metadata/artwork, thumbnails
generator, simple editing (trim/export).
**Done when.** Videos app and ojjyWeb `<video>`/`<audio>` use it.
**Tests.** Live playback tests with fixture files (dropped-frame and
A/V-offset counters).
**Pitfalls.** Decoders in a sandboxed helper process.
**Refs.** —

### MED-02 — Container demuxers
- Phase: P6 · Size: M · Status: todo
- Depends: USR-01
- Files: lib/libmedia/demux/

**Goal.** ISO BMFF (MP4/MOV/M4A incl. fragmented MP4), Matroska/WebM,
Ogg, WAV/AIFF, MP3 frames with ID3, ADTS AAC, FLAC native; robust
against truncated/corrupt files.
**Done when.** Fixture corpus parses; fuzzing clean.
**Tests.** Host tests + fuzzing.
**Pitfalls.** Box/element size overflows.
**Refs.** ISO/IEC 14496-12, Matroska spec (IETF RFC 9559).

### MED-10 — Audio decoders
- Phase: P6 · Size: L · Status: todo
- Depends: MED-02
- Files: lib/libmedia/audio/

**Goal.** Original decoders: MP3 (layer III), AAC-LC + HE-AAC (SBR, PS),
FLAC, ALAC, Vorbis, Opus (SILK + CELT hybrid). One sub-step per codec.
**Done when.** Each decoder matches reference decodes within the
spec's conformance tolerance on test streams.
**Tests.** Host conformance tests (reference PCM generated with host
tools as build-time fixtures), fuzzing.
**Pitfalls.** Opus and AAC are large: split into cards if needed.
**Refs.** ISO/IEC 11172-3, 14496-3; RFC 6716 (Opus); RFC 9639 (FLAC).

### MED-11 — Audio encoders
- Phase: P6 · Size: M · Status: todo
- Depends: MED-10
- Files: lib/libmedia/audio/enc/

**Goal.** Opus encoder (CELT/SILK modes, VBR), FLAC encoder, WAV writer;
used by Voice Memos, screen recordings, ojjyWeb (WebRTC later).
**Done when.** Encoded files decode correctly on the Mac.
**Tests.** Round-trip quality tests (SNR thresholds).
**Pitfalls.** —
**Refs.** RFC 6716.

### MED-20 — Video encoder for screen recordings
- Phase: P6 · Size: L · Status: todo
- Depends: MED-21
- Files: lib/libmedia/video/enc/

**Goal.** Original encoder producing a widely playable format for screen
recordings (H.264 constrained baseline with I/P frames, or VP8 — decide
in a decision record weighing simplicity vs. compatibility), real-time at
1440×900 30 fps on 4 cores.
**Done when.** Screen recordings play in QuickTime/Chrome on the Mac.
**Tests.** Decode-back tests with our decoder and host players.
**Pitfalls.** Rate control.
**Refs.** ITU-T H.264.

### MED-21 — H.264/AVC decoder
- Phase: P6 · Size: L · Status: todo
- Depends: MED-02
- Files: lib/libmedia/video/h264/

**Goal.** Baseline/Main/High profile (CABAC, CAVLC, B-frames,
interlaced optional), SIMD IDCT/MC/deblocking, slice/frame threads;
1080p30 real time under VZ.
**Done when.** Conformance bitstreams decode bit-exactly; performance met.
**Tests.** Conformance suite (freely available JVT streams), fuzzing.
**Pitfalls.** Very large — split by profile features before starting.
**Refs.** ITU-T H.264.

### MED-22 — AV1 decoder
- Phase: P7 · Size: XL · Status: todo
- Depends: MED-02
- Files: lib/libmedia/video/av1/

**Goal.** AV1 Main profile decoder (8/10-bit, film grain) for AVIF and
modern web video; SIMD + threads. Must be split before work.
**Done when.** Argon conformance subset passes; 1080p30 real time.
**Tests.** Conformance streams.
**Pitfalls.** Huge spec: multi-card effort.
**Refs.** AV1 Bitstream & Decoding Process Specification.

### MED-23 — HEVC decoder (stills and video)
- Phase: P7 · Size: XL · Status: todo
- Depends: MED-02
- Files: lib/libmedia/video/hevc/

**Goal.** HEVC Main/Main10 decoding for iPhone HEIC photos and videos;
start with intra-only (HEIC stills), then full video. Split before work.
**Done when.** HEIC photos decode correctly (GFX-06).
**Tests.** Conformance streams.
**Pitfalls.** Patent landscape — decision record before shipping.
**Refs.** ITU-T H.265.

### MED-24 — VP9 decoder
- Phase: P7 · Size: L · Status: todo
- Depends: MED-02
- Files: lib/libmedia/video/vp9/

**Goal.** VP9 profile 0/2 decoding for YouTube-style WebM streams.
**Done when.** Conformance vectors decode; 1080p30 real time.
**Tests.** libvpx test vectors (data files).
**Pitfalls.** —
**Refs.** VP9 Bitstream Specification.

### MED-25 — Subtitles
- Phase: P6 · Size: S · Status: todo
- Depends: MED-01, TXT-11
- Files: lib/libmedia/subtitles/

**Goal.** SRT, WebVTT (with styling subset), SSA/ASS basic, embedded
text tracks; styling options in Accessibility (A11Y-10).
**Done when.** Subtitles show in Videos and ojjyWeb.
**Tests.** Parser fixtures (fuzzed).
**Pitfalls.** —
**Refs.** W3C WebVTT.

### MED-30 — Camera framework
- Phase: P6 · Size: M · Status: todo
- Depends: SEC-11, GFX-03
- Files: services/camerad/, lib/libcamera_ojjy/

**Goal.** Camera devices (UVC — DRV-USB-07, VZ camera if available,
virtual test camera), capture sessions (preview, photo, video), formats,
controls, permission + indicator; virtual camera for tests.
**Done when.** Camera app previews the virtual test camera in QEMU.
**Tests.** Live tests with the virtual camera.
**Pitfalls.** —
**Refs.** —

### MED-40 — HLS and DASH streaming
- Phase: P6 · Size: M · Status: todo
- Depends: MED-01, NET-17
- Files: lib/libmedia/streaming/

**Goal.** Adaptive streaming clients (HLS playlists with fMP4/TS, DASH
MPD), bandwidth estimation, quality switching; used by ojjyWeb (via
Media Source Extensions later) and Music radio.
**Done when.** A public HLS test stream plays with quality switching.
**Tests.** Live tests with local fixture streams.
**Pitfalls.** DRM-protected streams are a non-goal (`01-vision.md`).
**Refs.** RFC 8216, ISO/IEC 23009-1.
