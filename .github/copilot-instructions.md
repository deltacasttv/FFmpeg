# FFmpeg AI Assistant Instructions

These instructions apply to AI-assisted development in this repository
(DELTACAST's FFmpeg fork), whatever the assistant (GitHub Copilot, Claude
Code, ...). When in doubt, prioritize FFmpeg upstream rules from
`doc/developer.texi` and https://ffmpeg.org/developer.html.

## Language Policy

All code, comments, log strings, commit messages and technical documents are
written in English, whatever the language used in conversation.

## Core Principles

- Keep changes minimal, targeted, and easy to review.
- Do not mix functional and cosmetic changes in one patch.
- Preserve API/ABI compatibility unless a major-version policy explicitly allows
  a break.
- Treat all external/media input as untrusted.
- Prefer correctness, safety, and maintainability over speculative cleverness.
- Propose a technical change and wait for approval before implementing it.

## Project Scope (Videomaster-First)

- Only `libavdevice/videomaster_*.c`, `libavdevice/videomaster_*.h` and the
  `videomaster` section of `doc/indevs.texi` are DELTACAST code under active
  development.
- Everything else is upstream FFmpeg: inspect it when an issue points there,
  but assume it is correct by default.
- Avoid unrelated changes in other subsystems unless strictly required to fix
  build, correctness, or integration issues for Videomaster.
- If a broader refactor is needed, split it into separate reviewable patches.
- Keep `doc/indevs.texi` in line with the options and behavior in the code.

## VideoMaster Demuxer

### Channel types

- SDI/HDMI: video and audio properties are auto-detected from the signal, and
  both essences come from a single locked slot. `nb_channels`, `sample_rate`
  and `sample_size` only apply to SDI; HDMI deduces them from the stream.
- IP (SMPTE ST 2110): no auto-detection. Each essence is configured either
  explicitly (`ip_video_*` / `ip_audio_*` options) or from an SDP file
  (`ip_video_sdp_file` / `ip_audio_sdp_file`); the two ways are mutually
  exclusive. Explicit video needs width, height, frame rate, scan type and bit
  depth. Audio is L16 or L24 at 48 kHz, 1 to 64 channels.
- ST 2022-7 redundancy: `ip_*_sps_*` options, or a second `m=` entry in the
  SDP file. Source filtering: `ip_*_source` options, or `a=source-filter` in
  the SDP file.

### IP synchronization (`ip_sync`, default 1)

- `ip_sync 1` requires both essences. A StreamSync handle groups video (main)
  and audio (secondary); one lock returns both. A sync slot missing one
  essence delivers the other alone.
- The StreamSync lock sets its streams' I/O timeout itself and is bounded by
  the resync window: the sync handle rejects `VHD_SetStreamProperty`. Streams
  added to a StreamSync can't be stopped or closed directly.
- The SDK header documents `VHD_OpenStreamSyncHandle`'s resync window in
  90 kHz ticks, but the SDK implementation uses milliseconds.
- `ip_sync 0` with both essences: video and audio are independent streams.
  Audio is captured by a dedicated thread (`ip_audio_capture_thread()`) into a
  bounded queue that `read_packet` drains first. That thread must be stopped
  before `VHD_StopStream()` is called on the audio handle.

### Timestamps

- `timestamp_source`: `osc` (default), `system`, `hw`, `ltc_on_board`,
  `ltc_companion_card`, `rtp` and `ptp` (IP only).
- `ip_audio_timestamp_source` lets IP audio use its own source (default:
  follow `timestamp_source`). `osc`/`system` are one board-wide clock type, so
  the two options can't select different ones of those two.
- pts are normalized per essence: the first slot of each essence is 0.

### Reception loss and stopping

- Slot lock timeout: 1 s on IP, 5 s on SDI/HDMI. The interrupt callback is
  checked between locks.
- ffmpeg retries `EAGAIN` forever and `-t` can't fire without packets, so a
  capture that receives nothing only ends with `no_data_timeout`.
- Video and audio may share a multicast group: each (port, group) pair is
  joined and left once. The board doesn't send a new IGMP join after a link
  comes back, so the demuxer re-joins its groups after 1 s without data and
  when a port's link goes down then up.
- `read_header` propagates the real error code (`EINVAL`, `EBUSY`, ...).

### SDK

- In the usual build setup, the VideoMaster SDK headers are in
  `../../install/videomaster/include` relative to this repository. Ask for the
  SDK source location when an SDK behavior must be checked.
- Report SDK errors with `VHD_ERRORCODE_ToPrettyString`.

### Hardware and debugging

- Board and channel indices depend on the current rig: list them with
  `ffmpeg -sources videomaster,sources_loglevel=trace`.
- The user runs hardware tests. Never hard-kill a process using the SDK
  (`timeout`, `Stop-Process -Force`, ...): it wedges the driver and every later
  process fails with `0xC0000142` until the driver is reloaded.
- Debug with `-loglevel trace`: it logs the configuration, the
  `frames_received`/`frames_dropped` and
  `audio_slots_received`/`audio_slots_dropped` counters, and the SDK error
  strings. To isolate an essence on IP, use `ip_sync 0` and one essence.

## VideoMaster Validation Test Plan

Re-run the affected tests after a demuxer change, and extend the plan when a
feature is added. Capture cases pass with at most about 1% dropped
frames/slots over the run, plus a start-up gap of up to 150 ms. Error cases
must fail cleanly (no crash or hang) with the expected error code and message.

### IP (ST 2110)

| Test | Scenario | Expected |
|---|---|---|
| S1 | `ffmpeg -h demuxer=videomaster` | All `ip_video_*`, `ip_audio_*`, `ip_sync` options listed |
| S2 | Audio only, `ip_sync 0` | Clean capture, expected channel count at 48 kHz |
| S3 | Video only, `ip_sync 0` | Clean capture, expected resolution and frame rate |
| S4 | Video + audio, `ip_sync 1` | Clean capture, both essences paired |
| S5 | `ffplay`, `ip_sync 1` | Picture and sound correct and in sync |
| S6 | Both essences from SDP files, `ip_sync 0` | Clean capture |
| S7 | `ip_video_destination` + `ip_video_sdp_file` | `EINVAL`, "mutually exclusive" |
| S8 | `ip_sync 1` with a single essence | `EINVAL`, "ip_sync requires both video and audio" |
| S9 | Second instance on the same board/channel | `EBUSY` |
| S10 | ST 2022-7: unplug/replug one redundant link | No drop or pts gap |
| S11 | Video `osc`, audio `ip_audio_timestamp_source hw` | Clean capture, independent pts |
| S12 | `timestamp_source osc` + `ip_audio_timestamp_source system` | `EINVAL`, "can't both be 'osc'/'system'" |
| S13 | `timestamp_source rtp`, both essences | Clean capture, monotonic pts |
| S15 | `timestamp_source ptp`, both essences | Clean capture, monotonic pts; warning if PTP is not locked |
| S16 | Invalid `channel_index` (e.g. 999) | `EINVAL`, "Invalid channel index" |
| S17 | Explicit video config without `ip_video_width` | `EINVAL`, "ip_video_width is required" |
| S18 | SDP file with a second (SPS) `m=` entry, unplug/replug a link | Same as S10 |
| S19a | `ip_*_source` set to the real sender | Clean capture |
| S19b | SDP with `a=source-filter` | Clean capture, "SDP SSM: applying" in the trace log |
| S19c | `ip_*_source` set to a wrong sender | No data; ends on `no_data_timeout` |
| S20 | No redundancy: unplug/replug the link | Drop counters increase while down, no crash, stream resumes |
| S21 | `ip_audio_format L16` | `pcm_s16le` |
| S22 | `ip_audio_packet_time 125us` | Clean capture |
| S23a | `timestamp_source system` | Clean capture |
| S23b | `timestamp_source hw`, both essences | Clean capture |
| S24 | `ffmpeg -sources videomaster` | All boards/channels listed, no SDK error |
| S25 | `ffplay`, `ip_sync 0` | Picture and sound correct |

### SDI

| Test | Scenario | Expected |
|---|---|---|
| T1 | Basic capture | Signal's resolution, frame rate and scan type; no drop |
| T3 | Explicit `nb_channels`, `sample_rate`, `sample_size` | Each value applied |
| T4 | `timestamp_source rtp` | `EINVAL`, "only available for IP channels" |
| T5 | `dual_stream 1` on a 3G-B signal | Recognized as 3G-B dual link |
| T6 | Non-default `buffer_packing` (e.g. `YUV422_8`) | Matching pixel format |
| T7 | Interlaced signal | Correct field order |
| T8 | `ffplay` | Picture matches the signal |

### HDMI

| Test | Scenario | Expected |
|---|---|---|
| T2 | Basic capture | Clean capture, embedded audio auto-detected |
| T4 | `timestamp_source ptp` | `EINVAL`, "only available for IP channels" |
| T9 | `ffplay` | Picture correct, embedded audio audible and in sync |

## Search Commands (Multi-OS)

- Prefer `rg` (ripgrep) for text and file discovery on all platforms.
- On Windows/PowerShell, do not suggest `grep` as the default command.
- On Windows, prefer native PowerShell cmdlets when they are more efficient for
  the task (e.g. `Get-ChildItem`, `Select-String`, `Get-Content`).
- Use `rg --files` for file listing and `rg <pattern>` for content search.
- Only fall back to `grep` on Unix-like systems if `rg` is unavailable.

## MCP Workflow (Codebase Memory)

This workspace may expose a `codebase-memory-mcp` server with an indexed code
graph. Use it to navigate FFmpeg efficiently, then verify in source files before
editing.

1. Resolve the indexed project name first with `list_projects`, matching the
   entry whose `root_path` is this workspace. Do not hardcode project names.
2. Prefer `search_code` for broad discovery and impact triage; use `rg` or
   PowerShell equivalents for exact checks.
3. Use `get_architecture` and `detect_changes` before refactors.
4. Re-index with `index_repository` after substantial structural changes
   (`fast` is usually enough).
5. The graph is an accelerator, not source-of-truth: confirm all edits with
   direct file reads and build/test feedback.

If the SDK is indexed too, search both projects to confirm names, types and
call sequences.

## MCP Workflow (Context7)

Use `context7` for external references (tooling behavior, standards context,
third-party APIs), not as authority for FFmpeg-internal policy.

1. Resolve the library ID first, then query one concept at a time.
2. Keep queries specific and implementation-focused.
3. Cross-check with FFmpeg local docs (`doc/*.texi`) and existing code patterns.

## FFmpeg Coding Constraints (Must Follow)

### Language and Portability

- Main code: ISO C11. Public headers must remain C99 compatible.
- Do not use VLAs or C99 complex numbers.
- Keep compiler-extension use optional and non-essential.
- For SIMD/DSP additions, keep a plain C fallback when appropriate.

### Formatting and Style

- K&R style, 4-space indentation.
- No TAB characters outside Makefiles.
- No trailing whitespace.
- Keep lines readable (around 80 columns when it improves readability).
- Avoid unnecessary parentheses/casts.
- Use braces only when needed by structure/readability.
- If editing a non-conforming file, only normalize touched regions.
- Clear names and small functions do most of the explaining. Comments stay
  short and only say what the code can't: a non-obvious reason, a hidden
  constraint, a workaround. No narration, investigation history, dates or test
  references.

### Naming and Symbol Scope

- Functions/variables/members: lowercase_with_underscores.
- Types: CamelCase.
- Internal cross-file symbols: `ff_` prefix.
- Internal cross-library symbols: `avpriv_` prefix.
- Public symbols must use each library's established prefix conventions.

### Safety and Behavior

- Check and propagate error returns.
- Use `av_malloc()` family for allocations (unless external API requires
  otherwise).
- Handle allocation failures and return `AVERROR(ENOMEM)` when applicable.
- On invalid input, fail safely (commonly `AVERROR_INVALIDDATA`).
- Do not use `printf`/stdio in library code; use `av_log()`.
- Avoid undefined behavior, data races, leaks, and signed overflow.

## Patch and Commit Policy

- Keep commits logically split and independently reviewable.
- Commit message format:
  - `area: short summary`
  - Follow with clear what/why details and references (bug IDs, CVEs, threads).
- Keep bugfixes focused, especially for backports.
- Add proper license header for any new file, following FFmpeg templates.
- Do not submit unfinished enabled code that breaks build/tests.

## Testing Expectations

- On Windows, the build environment is msys2 with MSVC. Do not compile or run
  anything yourself: give the user the commands to run in msys2 and ask for
  the results.
- Have relevant build/tests run before a change is considered done.
- Prefer `make fate` for final validation when feasible.
- For assembly changes, add/update `checkasm` tests.
- Use `tools/patcheck` for style/policy checks before submission.
- Add regression tests for new modules/features when practical.

## API and Documentation Changes

- For public API changes in installed headers:
  - maintain backward compatibility within major version,
  - document with Doxygen comments,
  - update `doc/APIchanges`,
  - bump versions according to FFmpeg policy.
- Keep user-facing docs and changelog entries aligned with behavioral changes.

## Practical Agent Checklist

Before editing:

1. Identify subsystem owner/context (`MAINTAINERS`, nearby patterns).
2. Map impact with MCP search/architecture tools.
3. Confirm exact edit locations from source files.

While editing:

1. Keep patch minimal and focused.
2. Preserve existing conventions in touched files.
3. Avoid opportunistic cleanup unrelated to the fix.

After editing:

1. Run formatting/policy checks (`tools/patcheck`).
2. Have targeted tests and, when possible, FATE run.
3. Verify no unintended API/ABI regressions.
4. Prepare a review-friendly commit message with rationale.
