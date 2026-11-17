# C6 Fork Preparation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task.

**Goal:** Prepare a local, reviewable fork preserving both original S3 boards.

**Architecture:** C6 remains an isolated target with separate SDK/tooling and CI.
Existing S3 paths match merged upstream1.4.0. Installed dev.5 stays untouched;
fork dev.6 builds are distinctly labeled before any later installation.

**Tech Stack:** Python3.12, ESP-IDF5.4.2(S3)/5.5.2(C6), Git, existing Actions.

**Spec:** [Local fork preparation](../specs/2026-11-17-c6-fork-preparation.md)

## Global Constraints

- No GitHub fork/repository creation, push, PR submission, release or flashing.
- Keep original S3 firmware/tools and board matrix byte-for-byte upstream.
- C6 SDK5.5.2 separate; original SDK5.4.2 remains required by original builder.
- Preserve verified dev.5 branch/packages/private backup; no private data export.
- Scope code edits to new C6 tools/tests and additive CI; no runtime redesign.

## Task 1: Preserve verified build and integrate upstream

- [x] Record owner-confirmed four-way face result, retain auto and reopen app.
- [x] Archive verified dev.5 binary/source with unchanged SD package.
- [x] Preserve codex/c6-lcd-147-verified-dev5 and create codex/c6-lcd-147-fork.
- [x] Merge upstream main1.4.0 without conflicts and compare S3 source paths.
- [x] Regenerate fork SD catalog0.2.1-sd; bump only fork C6 source todev.6.

## Task 2: Reproducible C6 tooling and additive CI

Own new tools/c6_build.py, tools/c6_package.py, tests/test_c6_build_package.py,
and .github/workflows/build.yml only. No firmware/asset/target mutations: the
original S3 builds run concurrently and fingerprint those paths. No other
implementer, subagents, device, app, private backup or remote write operations.

- Add C6 builder analogous to existing tools/build.py. Use existing IDF5.5.2
  shell/Python; reject wrong SDK before building. Default build/config paths
  remain firmware/build-c6-lcd-1.47 and firmware/sdkconfig.c6-lcd-1.47.
  Generated assets must already exist (run converter with tools .venv first).
  Invoke reviewed target CMake with trace OFF and record c6-build.json only
  after successful build. Bind source/config/lock/output hashes. Hash actual
  relevant source while excluding managed_components, build/cache outputs and
  private paths so initial SDK dependency installation cannot change source
  fingerprint; generated headers and asset source/metadata must remain bound.
- Add C6 packager reusing copy_dependency_notices from original release tool;
  leave that original tool unchanged. Inputs default to C6 build and generated
  flash/SD asset directories; --output selects a fresh directory under dist.
  Validate build fingerprint, esp32c6 target,8MB config, project/base version
  versus c6_version.h full version, actual SDK flash map{0,0x8000,0x10000},
  assets at0x200000 and reviewed region capacities. Reject escapes, stale source/
  images/config/assets, incomplete inputs and existing outputs before publishing
  artifacts. Atomic temp staging is preferred. No fabricated clean/source SHA.
  Emit explicit experimental taby-c6-development-v2 manifest, four images with
  offsets/size/hash, target guide/root LICENSE/NOTICE, dependency notices,
  installation instructions preserving NVS, and SHA256-checked ZIP. Also make
  a separate full-SD ZIP from taby/ after validating metadata/hash; label SD
  version/checksum in manifest. No whole-chip erase or unrelated file copying.
  No source/Git bundle or PR output is requested; do not add a publish action.
- Keep new modules focused. Builder may expose its C6 fingerprint helpers to
  packager; no new generic framework or changes to legacy common.py.
- TDD synthetic temp input trees: valid package/hash/notice/image offsets,
  mismatched SDK/target/flash map/config, stale source and tampered image/assets,
  path escape/oversize, incomplete build, and overwrite refusal preserving
  previous files. Do not require physical hardware or real full IDF build in
  unit tests; substitute process/build boundary only. Run focused tests while
  iterating and one full host suite after final changes; self-review/owned commit.
- Preserve original firmware CI job/matrix and SDK5.4.2. Add separate C6 job
  using espressif/idf:v5.5.2, generate flash/full SD assets, run c6_build.py,
  execute host tests with requirements-c6 installed and LVGL dependency present,
  then c6_package.py and upload local artifact only. Keep SDK Python environment
  separate from .venv converter/test environment (do not downgrade SDK tools).
  Existing check job may install requirements-c6 instead of base so image tests
  run; C6 job runs real renderer checks after managed LVGL is available.

Report .superpowers/sdd/2026-11-17-c6-fork-preparation/task-2-report.md with
red/green, commits, exact commands/results and remaining checks. Save full host
output in .local/c6-fork-tooling-host-tests.log. Parent reviews/builds/packages.

## Task 3: Compatibility validation

- [x] Original IDF5.4.2 builds/packages amoled-1.64 and round-1.32; report actual
  sizes/hashes/warnings. Do not edit source to make an inherited issue disappear.
- [x] Parent rebuilds/packs C6 via new tools and verifies all3367 full-SD frames
  with real decoder; full host suite and asset/layout checks pass.
- [x] Review CI and fork delta; audit source/history/export for private artifacts.
- [x] Record separate installed dev.5 evidence and unflashed dev.6 build evidence.

## Task 4: Local handoff, no push

- [x] Configure local upstream original and origin intended personal fork URL;
  no remote repository creation. Keep source/development branch recoverable.
- [x] Add fork/build guide without creating any PR or draft.
- [x] Check local build/package outputs; ensure a clean working tree and exact
  upstream/S3 preservation gate. No extra Git bundle/patch or PR draft requested.
- [x] Final report names branch/path/artifacts and states nothing was pushed.
