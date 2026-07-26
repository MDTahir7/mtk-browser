# ADR-0001: Direct Chromium as the browser foundation

**Status:** Accepted for planning; implementation approval pending.  
**Date:** 2026-07-23

## Context
The product needs an Android-first, privacy-focused, proprietary browser with browser-engine controls for filtering, policy enforcement, sandbox preservation, multi-tab browsing, permissions, crash recovery and native desktop support.

## Decision
Use direct Chromium as the rendering/browser foundation. Use C++ for Chromium-facing integration; Kotlin/Java only for Android application and JNI-facing integration; Chromium Views and native APIs for desktop. Create original product identity, application identifiers and icons. Keep policy, filtering, branding, block-page UI and updates as separate proprietary modules.

## Alternatives rejected
- **Brave Core:** rejected because the product must not copy its MPL-licensed implementation or inherit file-level MPL source-disclosure duties.
- **Firefox/GeckoView:** rejected as the requested engine and proprietary-integration strategy is Chromium-centric; its licence/dependency posture would require a separate assessment.
- **Electron/WebView/Flutter WebView/React Native WebView:** rejected as primary architecture because they do not provide the required direct Chromium browser-process, policy, release and patch-control model.

## Consequences
This enables original browser-level controls but requires expensive upstream rebasing, security response, native build infrastructure, full third-party notice management, and a supported Linux Android build environment. Filtering is defence in depth, not a guarantee; all block decisions must retain reason, category, policy source, confidence and timestamp.

## Guardrails
Preserve Chromium sandboxing/site isolation/TLS validation; no unrestricted JavaScript bridge; least privilege; OS-backed administrator secrets; signed policy/filter updates; on-device filtering by default; no external upload of sensitive browsing content without explicit consent.