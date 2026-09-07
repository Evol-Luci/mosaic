# Audio A3 — cooked audio-clip asset — Design

## Goal (Phase 4, Audio pillar — slice 3)

Let a project **import a `.wav` and reference it as an audio asset**, so an
`AudioSource.clip` (an `AssetID`) resolves to decoded PCM the miniaudio backend
(A2) can play. Mirrors the T5 image pipeline exactly (`read_ppm`/`cook_image`/
`uncook_image` + the `import_asset`/`load_project_assets` dispatch): parse the WAV
at import, cook a compact PCM blob + a `.toml` sidecar, and register the loaded
`AudioClip` as an `AssetRegistry` resource at startup. Fully headless.

**Scope: the asset pipeline only.** Feeding loaded `AudioClip`s to
`backend->register_clip`, injecting the real device backend, wiring `AudioSystem`
into `setup_runtime`, and a demo sound are **A4**. A3 verifies cook/load headlessly
and, as an end-to-end check, plays a loaded clip through A2's **null** backend.

## Design

### `engine/audio/audio_clip.hpp` — neutral resource

```cpp
struct AudioClip
{
    std::vector<float> pcm;         // interleaved f32 samples (channels-major per frame)
    std::uint32_t      channels    = 0;
    std::uint32_t      sample_rate = 0;
    std::uint64_t      frames      = 0; // pcm.size() == frames * channels
};
```

A runtime resource like `render::Image` — **not** a reflected component; it is
registered via `AssetRegistry::register_resource<AudioClip>` and resolved by
`AssetID`.

### `app/asset/audio_cook.hpp` — parse + cook + uncook (mirrors `image_cook.hpp`)

- `read_wav(const std::string& bytes) -> std::optional<AudioClip>` — a minimal
  RIFF/WAVE parser:
  - Verify `"RIFF" <u32 size> "WAVE"`.
  - Walk chunks (`<4-char id><u32 size><payload>`, payloads word-aligned/padded to
    even length), capturing the `"fmt "` and `"data"` chunks; skip any others
    (`LIST`, `fact`, …).
  - `fmt `: `audioFormat u16`, `channels u16`, `sampleRate u32`, `byteRate u32`,
    `blockAlign u16`, `bitsPerSample u16`. For `WAVE_FORMAT_EXTENSIBLE` (`0xFFFE`),
    take the real format tag from the first 2 bytes of the SubFormat GUID in the
    extension.
  - **Supported:** `audioFormat==1` with `bitsPerSample==16` (integer PCM), and
    `audioFormat==3` with `bitsPerSample==32` (IEEE float). Anything else → nullopt
    (logged by the caller).
  - Convert `data` samples to interleaved f32: 16-bit → `int16_le / 32768.0f`;
    float32 → bit-cast the 4 LE bytes. `frames = dataSize / (channels *
    bytesPerSample)`.
  - Reject (nullopt) on: bad magic, missing `fmt `/`data`, `channels==0`,
    `sampleRate==0`, unsupported format/bits, `dataSize` not a multiple of
    `blockAlign`, or truncation.
- `cook_audio(const AudioClip&) -> std::string` — on-disk form:
  `"MOSAAUD1"` + `u32 channels` + `u32 sample_rate` + `u32 frames` (LE) + the raw
  f32 sample bytes (`frames*channels*4`). (Frames are capped to `u32`; a clip over
  ~4 G frames is rejected at cook — far beyond any real sound.)
- `uncook_audio(const std::string&) -> std::optional<AudioClip>` — inverse: check
  magic + that the byte length is exactly `20 + frames*channels*4`; nullopt
  otherwise.

Little dependency, header-only, no miniaudio — the wall stays intact (decode
happens at cook via our own parser).

### `app/asset/asset_io.hpp` — dispatch (extend, don't restructure)

- `import_asset`: add a `.wav` branch — `read_wav(*src)` → nullopt+log if not a
  supported WAV, else `blob_bytes = cook_audio(clip)`, `blob_ext = ".audioclip"`,
  `asset_type = "audio"`. Same overwrite-refusal + metadata-rollback as images.
  (`.wav` is binary; the existing `load_scene_file` used to read the source reads
  the file as raw bytes, which is correct for binary too.)
- `load_project_assets`: add a `meta.type == "audio"` branch — read the sibling
  `.audioclip` blob, `uncook_audio` it (warn+skip on missing/corrupt), and
  `registry.register_resource<AudioClip>(meta, make_shared<AudioClip>(...))`.

## Testing (headless, TDD)

New `tests/unit/app/audio_asset_test.cpp` (tag `[app]`), plus the test file added
to `tests/CMakeLists.txt`. A small in-test helper builds a canonical 16-bit-PCM WAV
byte string (and a float32 one) from generated samples.

1. **read_wav 16-bit PCM** — a generated mono/stereo 16-bit WAV parses to the
   expected `channels`/`sample_rate`/`frames`, and a full-scale sample maps near
   `±1.0` f32.
2. **read_wav float32** — a 32-bit-float WAV round-trips its sample values into
   `pcm` (bit-exact).
3. **read_wav rejects junk** — non-`RIFF` bytes, a truncated file, a missing
   `data` chunk, and an unsupported bits/format all return nullopt.
4. **cook/uncook round-trip** — `uncook_audio(cook_audio(clip))` reproduces
   `channels`/`sample_rate`/`frames`/`pcm`; a bad magic and a size-mismatched blob
   return nullopt.
5. **import → load round-trip** — in a temp project (`create_project` +
   `import_asset(project, wav)`), `load_project_assets(registry, assets_dir)`
   registers exactly one asset, and `registry.load<AudioClip>(id)` returns the
   clip with matching metadata.
6. **plays through the null backend (end-to-end)** — feed a loaded `AudioClip`'s
   PCM to `create_miniaudio_backend(true)->register_clip(...)` then `play(...)`;
   the returned voice is non-zero and `is_playing` is true — proving the A3 asset
   drives the A2 backend.

## Out of scope / follow-ups (A4 and beyond)

- **A4:** at app startup, iterate registered `AudioClip`s and call
  `backend->register_clip(id, ...)`; inject `create_miniaudio_backend()` (real
  device); wire `AudioSystem` into `setup_runtime`; add a demo `AudioSource` +
  `AudioListener` + a bundled `.wav`.
- 24-bit / 8-bit PCM, compressed (`.mp3`/`.ogg`) decode.
- Streaming very long clips instead of full in-memory decode.
- Resampling clips to the engine rate at cook (miniaudio resamples at play today).
