# Free A11 7.3.1 Phone producer tests

Run with JDK25 and existing normal prepared Java dependencies: `java src/prepare/test/phone-producer/Run.java ROOT EXACT_PHONE_APK`.

The optional third argument selects an isolated source overlay for review. The runner uses normal `.local/toolchains/java/libraries`, SDK34 platform, D8 and APK signing libraries; it downloads nothing and changes no canonical source or OEM input. It retains bounded evidence under a fresh `.local/build/jobs/phone-producer-tests-UUID` directory.

Tests cover semantic owner/member inverse contracts independent of input acquisition pins; manifest ownership, provider permissions and binary XML preservation; wrong profile/API/theme/duplicate/truncated manifests; shared provider source integrity and own-only API30 D8 output; source metadata API matching; ordinary source ProfileRegistry and Artifacts peer/Phone graph, local platform signature and original code/asset byte preservation.

The signed package check reads the existing profile platform signing inputs and produces only a local review artifact. It does not install, start a guest, change host capability or prove runtime acceptance.
