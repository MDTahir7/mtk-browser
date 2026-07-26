# Upstream Chromium Strategy

## Foundation
Use direct Chromium, pinned to a reviewed commit and its `DEPS` graph. The product may be proprietary, but upstream licence notices and any component-specific obligations remain mandatory. Upstream build requirements change; consult [Windows instructions](https://github.com/chromium/chromium/blob/main/docs/windows_build_instructions.md) and [Android instructions](https://github.com/chromium/chromium/blob/main/docs/android_build_instructions.md) at the selected revision.

## Separation model
- `chromium/` is an unmodified pinned upstream worktree, never edited for product logic.
- `patches/chromium/` contains minimal, reviewable integration patches with upstream commit, rationale, owner and rebase status.
- `proprietary/` owns filtering, policy, branding and UI business logic behind narrow C++/JNI interfaces.
- `vendor/` stores approved third-party manifests, signatures, licences and SBOM inputs—not unreviewed binary blobs.

## Security-update process
1. Monitor Chromium stable/security advisories and the pinned branch daily.
2. Triage relevant CVEs, upstream commits and patch conflicts within one business day.
3. Rebase the patch series, regenerate notices/SBOM, run sandbox/site-isolation regression tests, and publish an updated build through a signed release pipeline.
4. Track every skipped patch with risk owner and expiry; never disable TLS verification, sandboxing or site isolation to ease integration.

## Platform position
- Windows desktop: Chromium Views + native Windows integrations; current upstream guidance requires Visual Studio tooling and Chromium's own clang-cl flow.
- Android-first: build on a supported Linux host; Windows is not the supported Android Chromium build environment.
- No Electron, WebView, GeckoView, Flutter WebView, React Native WebView or Brave Core is part of the target architecture.

## Milestone 0 boundary
No upstream repository, `depot_tools`, artifacts, or build cache has been downloaded. Content was rephrased for compliance with licensing restrictions.