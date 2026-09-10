# FFmpeg Copilot Instructions

These instructions apply to AI-assisted development in this repository.
When in doubt, prioritize FFmpeg upstream rules from `doc/developer.texi` and
https://ffmpeg.org/developer.html.

## Core Principles

- Keep changes minimal, targeted, and easy to review.
- Do not mix functional and cosmetic changes in one patch.
- Preserve API/ABI compatibility unless a major-version policy explicitly allows
  a break.
- Treat all external/media input as untrusted.
- Prefer correctness, safety, and maintainability over speculative cleverness.

## Project Scope (Videomaster-First)

- Work in this repository is almost exclusively focused on the Videomaster
  demuxer area.
- Prioritize changes in `libavdevice/videomaster_*.c`,
  `libavdevice/videomaster_*.h`, and input-device documentation in
  `doc/indevs.texi`.
- Avoid unrelated changes in other subsystems unless strictly required to fix
  build, correctness, or integration issues for Videomaster.
- If a broader refactor is needed, split it into separate reviewable patches.

## Search Commands (Multi-OS)

- Prefer `rg` (ripgrep) for text and file discovery on all platforms.
- On Windows/PowerShell, do not suggest `grep` as the default command.
- On Windows, prefer native PowerShell cmdlets when they are more efficient for
  the task (e.g. `Get-ChildItem`, `Select-String`, `Get-Content`).
- Use `rg --files` for file listing and `rg <pattern>` for content search.
- Only fall back to `grep` on Unix-like systems if `rg` is unavailable.
- Keep `grep_search` (tool) usage for indexed workspace queries when it is more
  efficient than shell commands.

## MCP Workflow (Codebase Memory)

This workspace may expose a `codebase-memory-mcp` server with an indexed code
graph. Use it to navigate FFmpeg efficiently, then verify in source files before
editing.

1. Resolve the indexed project name first.
   - Call `mcp_codebase-memo_list_projects`.
   - Match the entry with `root_path` for this workspace.
   - Do not hardcode project names.
2. Prefer graph-assisted search for exploration.
   - Use `mcp_codebase-memo_search_code` for broad discovery and impact triage.
   - Use `grep_search`, `rg`, or PowerShell equivalents for exact checks.
3. Use architecture/diff intelligence before refactors.
   - `mcp_codebase-memo_get_architecture` for subsystem boundaries and hotspots.
   - `mcp_codebase-memo_detect_changes` to estimate blast radius.
4. Re-index after substantial structural changes.
   - Run `mcp_codebase-memo_index_repository` (`fast` is usually enough for
     iterative work).
5. The graph is an accelerator, not source-of-truth.
   - Confirm all edits with direct file reads and build/test feedback.

## Local SDK Context (Cross-Repo)

- The Videomaster SDK is available locally at `C:/Local/sdk`.
- This SDK is also indexed by `codebase-memory-mcp` and should be used for
  interface checks and behavior alignment.
- For SDK/API investigations:
  1. Call `mcp_codebase-memo_list_projects` and identify both the FFmpeg
    workspace project and the SDK project by their `root_path`.
  2. Run `mcp_codebase-memo_search_code` in both projects to confirm naming,
    types, and expected call sequences.
  3. If cross-repo call/channel mapping is needed, use
    `mcp_codebase-memo_index_repository` in `cross-repo-intelligence` mode
    after ensuring both indexes are fresh.

## MCP Workflow (Context7)

Use `context7` for external references (tooling behavior, standards context,
third-party APIs), not as authority for FFmpeg-internal policy.

1. Resolve library ID first with `mcp_context7_resolve-library-id`.
2. Query one concept at a time with `mcp_context7_query-docs`.
3. Keep queries specific and implementation-focused.
4. Cross-check with FFmpeg local docs (`doc/*.texi`) and existing code patterns.

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

- On Windows, msys2 is used as the build environment. Do not try to compile or execute anything and ask me to do it for you. You can ask me to provide the commands to run in msys2.
- Run relevant build/tests before proposing changes.
- Prefer running `make fate` for final validation when feasible.
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
2. Run targeted tests and, when possible, FATE.
3. Verify no unintended API/ABI regressions.
4. Prepare a review-friendly commit message with rationale.
