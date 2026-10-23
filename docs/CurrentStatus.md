# Current Status

Updated 2026-10-23. This page describes the current implementation, release
limitations and tested artifacts. Public version history remains in
[CHANGELOG](../CHANGELOG.md); commands and acceptance procedures belong in
[Build And Release](BuildAndRelease.md) and [In-Game Validation](InGameValidation.md).

## Cached-song startup cost — Integration in progress

- The owner clarified that the hitch occurs during initial song loading, not
  selection after discovery. A stable filesystem log snapshot is retained under
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/cached-song-latency-20261023T102732Z-75d63ee8`
  (`observation.json`, `song-load-summary.json`). Its producer is not authenticated
  merely by the separately observed installed DLL hash; no game process was
  controlled, cache deleted, or installed artifact replaced for this investigation.
- All 31 candidates report warm artifact acceptance, followed by about 25 seconds
  cumulatively in descriptor/native-cache admission intervals. Repository trace
  strings are replayed at settlement: their adjacent log timestamps do NOT measure
  original hashing/validation duration. This source-backed timing correction is
  retained in `song-load-summary.json`. Existing native-updater timers independently
  show discovery/binding passes growing into tens of milliseconds on the qualified
  updater thread. These observations are not an FPS or per-function CPU profile.
- Scoped implementation preserves full content verification and native readiness:
  Windows SHA-256 acceleration with scalar fallback, hoisted immutable discovery
  name comparisons, and one operation-local binding read journal retaining both
  immediate-record and final-cohort stability verification. The production proxy
  builds and all ten affected checks pass, with clean scoped independent reviews.
  Digest equivalence/failure cleanup/concurrency and binding drift/identity/retention
  checks pass. Watched binding reads are 3 rather than 4; native mount/request/bridge
  counts are unchanged. An isolated 2,097,169-byte SHA test takes 8424 us scalar
  versus 867 us through CNG, not an overall startup/FPS measurement.
- Frozen candidate: `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-song-startup-candidate-20261023T105350Z-ca03eb29`,
  DLL SHA256 `7db98320fb39ff089959747ae7a5483eb779c39227abf62ca7a53b90052fdb0b`.
  Exact production inputs, current focused tests, provenance/cache, build/CTest
  logs and validation are retained. Only CMake and the three scoped production
  implementations differ from the current kept chart-admission receipt; unrelated
  chart integration is preserved. Installed without rebuilding in prepared session
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/native-proxy-20261023T110359Z-b16a74d979b5449286dafd70e5240b3a/session.json`.
  The owner explicitly authorized closed-game installation; the prior kept chart
  proxy was canonically rolled back before exclusive preparation. Its DLL/log/
  receipt and the independently reviewed transaction are retained under
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-opt-install-20261023T110112Z-bf6ac289`.
  INI/Themes are unchanged; songs/caches were not touched and no game was launched.
  Physical startup improvement remains unqualified. Default cohort/cache tests do
  not replace optional real-donor checks.
  Native request internals and the two complete object scans remain outside this
  batch. No elapsed preparation limit or new time-based scheduler is introduced;
  caches/formats/content identity remain unchanged, so invalidation is unnecessary.

## Custom chart admission — Integration candidate; gameplay unresolved

- Static lookup/actual-dispatch contract:
  `analysis/ChartAdmissionReadOnlyLookupEvidence-20261023.md`.
- Older-build extension passed independent review and all four focused Release
  selftests (`native_audio_runtime_selftest`, `native_audio_adapter_selftest`,
  `native_proxy_bootstrap_selftest`, `chart_selected_row_selftest`) under each
  catalog selection. The default build configuration was restored afterward;
  build/test logs are retained under
  `C:/Users/makci/AppData/Local/Temp/opencode/lookup-1004-baseline-20261023T140213092/`.
  All optional dispatch targets were independently recovered from the
  older Ghidra program. Its different getter slots now come from the existing
  exact-build `NativeAudioLayout`, rather than the newer hardcoded pair. Both
  catalog selections execute the same traversal/concurrency fixtures, including
  per-target signature rejection. Newer catalog data/generated headers are
  unchanged. No older disk authentication or in-game acceptance is claimed;
  this source change has not replaced the currently kept installed proxy.
- A scoped actual-dispatch implementation now permits full binding validation
  during qualified read-only lookup dispatch. Registration exclusion remains;
  unknown/cold dispatch and nested mutations retain conservative protection.
  Whole-population preclassification was rejected because existing witnesses
  do not pin every visited provider; the implementation does not rely on it.
  Unsupported builds/capabilities and unsuitable output storage use the original.
  Focused runtime/adapter/bootstrap/selected-row tests and older-catalog runtime
  test passed. Independent review found no blockers; production proxy compilation
  and canonical build-provenance verification passed. The reviewed candidate is
  installed with explicit authorization; a bounded human run is collected/reviewed.
  Genuine mutation
  contention and safe native cancellation remain unresolved, so this is not a
  complete chart/audio-split fix or broad gameplay qualification.
- New dispatch signatures match the authenticated installed executable on disk;
  `lookup-dispatch-disk-verification.json` in the capture root records exact
  catalog/game hashes and checked bytes. This is not live execution evidence.
- Chart-admission validation session was collected/Kept after the scoped successful
  run, before the startup candidate above replaced its installation:
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/native-proxy-20261023T093952Z-7c5767add3ef40129c856b7fd59b223f/session.json`.
  All four Lv6 launches (Libera Me From Hell, Mikus Song, Hacking to the Gate,
  Promise) authenticate their custom chart targets, commit preparation, and
  produce matching native event counts with valid captures. No manager-exclusion
  rejection is logged; user reports apparently correct gameplay. Superseded
  prior generations account for the three terminal `failed_recorded/outcome=9`
  markers, not new-chart failure. Sorairo Days and extended-tail charts were not
  launched in this run; recurrence/concurrent pure-path coverage is not proven.
  `manual-report.txt` and `keep-result.json` in the candidate evidence directory
  below bind this limited assessment and disposition.
  The original error log remains hash-identical. Candidate/source/provenance,
  previous diagnostic DLL and latest log, transition plan, deployment identity,
  and fallback receipt are preserved in the capture root below under
  `lookup-admission-20261023T093949Z-6af233c6/`. Canonical rollback followed by
  that fallback-receipt prepare restores the previous proxy without overwrites.

- Preserved Sorairo Days (Gurren Lagann) report: chart planning succeeded, then
  final selected-row capture rejected with `manager_exclusion` before writes.
  The failed activation was Lv4 after switching through Lv6. This is separate
  from the fixed offline timestamp-rounding inconsistency; no provider drift
  or safe post-request native cancellation is established.
- Reviewed diagnostics preserve admission/locking semantics and distinguish
  nested entry from failed exclusive try. Observed native-entry categories are
  explicitly racy/non-authoritative, including an empty observed mask.
  `native_audio_runtime_selftest`, `chart_selected_row_selftest`, and production
  proxy compilation passed. This does not fix the chart/audio split.
- The diagnostic run did not reproduce the symptom. Collected log authenticates
  Sorairo Days Lv6/Lv5 and Rufus chart targets without manager-exclusion rejection;
  user reports no incorrect charts in that run. This is not a fix or evidence
  against the original failure. This diagnostic session was collected/Kept,
  then canonically rolled back for the authorized candidate replacement above:
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/native-proxy-20261023T075845Z-64ee9706e2d146778886f31f65b2011e/session.json`.
  Original failure log, prior DLL, source/provenance, diagnostic receipt, and
  fallback receipt are preserved under
  `%LOCALAPPDATA%/Temp/opencode/piano-vanilla-chart-20261023T102948061/`.
  `pre-diagnostic-20261023T105537649/diagnostic-deployment.json` records exact
  installed and rollback identities; `diagnostic-manual-report.txt` and
  `diagnostic-keep-result.json` document the bounded no-repro run. Restoring the
  prior proxy requires the canonical fallback-receipt prepare after rollback; rollback alone
  restores the prior ASI, not that proxy. No gameplay or Release pass is claimed.

## Menu themes — in development

- Integration optimization now targets the measured repeated proof work and
  unwanted physical preparation in Original: private native-call-delimited
  palette phases, untouched passive-source detachment, and demand-driven theme
  materialization. The exact pre-change source/test baseline is retained under
  `%LOCALAPPDATA%/Temp/opencode/theme-opt-baseline-20261022T112159Z-db931f4e`.
  Native/palette focused tests now demonstrate Linear qualification 7-to-2 and
  Subject validation 5-to-2 with unchanged Original/helper calls, plus guarded
  warm capture target validation 2-to-1 with preserved affinity. Review exposed
  uncertain failed restoration being mistaken for clean passive retirement;
  certainty/override bookkeeping and four failure variants now pass the affected
  tests, with clean independent recheck. Runtime demand preparation and metadata-only
  revocation guards are implemented and independently reviewed. Re-request canonical
  indices, rejected-demand cleanup and no-OS-call guards are corrected; the partial
  bind/yield/rejection regression failed before the cleanup fix and passes afterward.
  Fixtures show zero cold-Original materialization/binds/captures, zero retained
  palette routes after restoration/cancellation, and identical committed native
  operation counts with zero versus eight inactive themes. First explicit requests
  now bear deferred preparation cost; Original is still not a vanilla baseline.
  The production proxy built and ten affected tests passed. Frozen candidate
  SHA256 `92843afc9db77fa6c86b07311304084e183412a19f8d107a20c7b0ac76a56984`,
  matching production sources/provenance and focused validation are retained under
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-opt-candidate-20261022T123436Z-666caac4`.
  Following explicit owner authorization, this exact frozen DLL was installed
  without rebuilding through prepared session
  `native-proxy-20261022T125641Z-b52f743841ef48be9c704205a8a1ee08`.
  The prior kept diagnostic session was canonically rolled back first; its artifact,
  log and receipt are retained with the independently reviewed transaction under
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-opt-install-20261022T125452Z-002f76aa`.
  Closed-game ownership/hash/disabled-ASI and unchanged INI/theme checks passed;
   no automated launch occurred. The subsequent human run reports smooth Original
   but still unacceptable custom-theme FPS. Canonical collection and reviewed Debug
   `Keep` are complete for that session; this retains the development artifact, not
   custom-theme performance acceptance. Its `performance-report.json`,
   `analysis-summary.json`, `phase-analysis-settled.json` and reviewed report bind
   the fresh log SHA256 `9fc869306a338ec098f3600e53c152a3533fd3afa18d82b0d8f8c7cd5162e46e`.
   Across recorded menu scopes, callback qualification consumes 65.0% of summed
   self wall time and basic target validation 19.9%; instrumented native helpers
   total 26.7 ms. Settled Original windows, excluding requests/transitions, have no
   custom captures or passive binds. Custom row callbacks still reach 153.7 ms;
   the first completed close takes 212.9 ms. The final generation has no completed
   close timing and must not be assigned one. No menu roots were dropped or native
   helper faults recorded. Source inspection identifies full callback admission
   repeated inside native read-only target classification and warm composition,
   outside the palette-local proof sharing. A private classifier/preflight phase
   is the next bounded candidate; owning/Bottom helper boundaries still require
   fresh proof. The classifier-only Integration follow-up is implemented against
   that installed candidate's archived source. Song and Speed fixtures demonstrate
   qualification 6-to-1, basic checks 2-to-2, reader operations 9464-to-9464 and native
   calls 0-to-0. Exact native/palette tests passed and independent review found no
   issues. Private state is cleared through SEH-finally; nested/revoked phases fail
   sticky, null guards retain full qualification, and no target results are cached.
   Warm-composition reuse and broader runtime reconstruction remain outside this
   batch. The production proxy built and five affected functional tests passed.
   Candidate SHA256 `2f156345cd654d71fb0d87d09b3449993b933d3b43a62718fdf68444df30c9b7`,
   exact production source/provenance and scoped validation are retained under
   `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-native-read-candidate-20261022T142921Z-a374ca77`.
   The source snapshot also preserves an unrelated concurrent
   `extended_chart_eligibility.h` delegation to the existing time-string helper;
   that change was neither authored/reverted nor included in the native-perf review.
   Following explicit closed-game authorization, the exact frozen classifier DLL
   was installed without rebuilding through prepared session
   `native-proxy-20261022T143455Z-e0f935be77b4472dbb62d9af59ac84bc`.
   The previously kept optimization session was canonically rolled back first;
   its DLL/log/receipt and the independently reviewed plan/transaction are retained
   under `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-opt-install-20261022T143241Z-7f3206f4`.
   Ownership/hash, disabled ASI, closed-game and unchanged INI/theme inventory
   checks passed. No automated launch occurred. The subsequent human run reports
   generally acceptable navigation among already-visible rows, but recycled rows
   still stutter and opening-to-theme delay remains unacceptable. That session is
   now collected and reviewed Debug `Keep`, not performance/restoration acceptance;
   its reports and `analysis-summary.json` bind log SHA256
   `38b0bb96dc07ee6d02167ca1ebe9f48c2228414c71c77932089c9b8aba9a8c69`.
   Row-hook maximum is 75.2 ms; saved-theme opening-to-commit intervals are
   531–1001 ms. The first explicit request takes 1398 ms; its 4477-ms
   Ready-to-commit sample also includes human waiting. First admissions take
   28–79 dispatches. One menu root was dropped; no native helper failures were
   recorded. The final generation revoked ownership without close authority and
   has no completed close timing. Source inspection finds row-local mutations
   still reset/revisit whole-program preparation and lookup inventories. The next
   Integration change targets indexed Song-row reconciliation over existing
   records, fresh tickets/affinity/baselines and complete requested-theme admission.
   Preparation still has actual work plus gaps from the existing input poller;
   increasing slice size or unbounded continuation chaining is not the remedy.
   The indexed row-local source change passes its focused runtime build/test.
   Reconciliation uses 13 preparation visits and 4 lookup visits with both 2/32 rows
   and zero/eight inactive themes; unaffected native-operation deltas match. This
   demonstrates orchestration scaling, not newly eliminated unrelated native writes.
   Review found a revoked Speed row left queued after partial preparation/cancel;
   its removal and idle-settling/admission regression now pass. Final independent
   recheck found no issues. The production proxy built and eight affected checks
   passed. Frozen candidate SHA256
   `f3932cc95e8cfe1e9c86db47a73cd2e8c98f298b2deebd63592db94999ba943d`,
   exact production inputs, current theme selftests, provenance and validation logs
   are retained under
   `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-row-index-candidate-20261022T155542Z-cf41950d`.
   Only runtime.cpp differs among production inputs from the preceding classifier
   snapshot. Following explicit closed-game authorization, the frozen candidate was
   installed without rebuilding through prepared session
   `native-proxy-20261022T160040Z-3befa0108960461cbd123e344556364f`.
   The previous kept classifier session was canonically rolled back; its DLL/log/
   receipt and the independently reviewed transaction are retained under
   `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-opt-install-20261022T155923Z-e112cb0c`.
   INI/theme inventory is unchanged; no automated launch occurred.
   The subsequent human run still finds performance unsatisfactory. Canonical
   collection and reviewed Debug `Keep` succeeded, not performance acceptance;
   its reports bind log SHA256
   `29c98f359d18eb7005f09c8668503f5d50a4a6b7efdf3b9d13a61adb398ad76a`.
   Current scope self wall time is 60.4% callback qualification and 23.9% basic
   target checks, with no menu drops or instrumented native-helper failures.
   Native setup maxima are about 1 ms, while row Before reaches 16.1 ms and final
   whole-theme Apply reaches 78 ms; persisted-theme openings take 977–1291 ms.
   The next coherent Integration batch targets shared native read-only member/
   Subject preflight and capture/compose/tint resolution, constant carrier-name
   reuse, and demand-driven one-shot delayed metadata wakes through the existing
   title owner. User32 callback timers were rejected because existing worker-side
   teardown cannot drain their queued callback pointers. The intended threadpool
   timer only posts ticket metadata; native work remains in the existing qualified
   WndProc. Disable/cancel/drain/close must precede teardown without lock inversion.
   A separate static check did not prove setup appearance-neutral: setup reaches
   data-dependent animation-channel callbacks. Restoration and fresh baselines
   therefore remain; no skip-restoration fast path is justified by that research.
   Scheduler source and its two focused tests pass independent review and execution,
   including cancellation, stale delivery, failure fallback and callback drainage.
   Native/palette focused checks and final independent recheck also pass after
   null-guard/untracked-target containment revalidation was added. Real
   reader fixtures reduce warm Button resolution reads from 135640 to 10569 and
   ImageSet from 70356 to 9221, with zero native helper calls in both warm paths.
   Cold native helper counts remain unchanged; a separate controlled carrier-name
   cache fixture reduces lookups from 16 to 4 while preserving construction/copy
   counts. Runtime now consumes the grouped APIs for capture/composition and
   changed-member/Subject preflight; independent integration/fixture reviews are
   clean. The production proxy build and ten affected checks pass. Frozen candidate
   `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-batch-candidate-20261022T180739Z-ba3e63a9`
   retains exact production sources/provenance, current tests/docs and build/CTest
   evidence. DLL SHA256 is `58ea2cf38c3023b9191d145f29ca045d9bbc83588c4bdc1df3f766ebc7c8e243`;
   all 271 production inputs match and only the five reviewed native/runtime/title
   inputs differ from the preceding row-index source set. After explicit owner
   authorization and independent transaction review, installed the frozen DLL
   without rebuilding through prepared session
   `native-proxy-20261022T181240Z-39ef82b9b6c8473097ae6d552105aaf2`.
   Prior kept row-index proxy/log/receipt and the reviewed install journal remain at
   `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-opt-install-20261022T181058Z-c9a6ae19`;
   its owner was canonically rolled back before exclusive preparation. All settings
   and theme files are unchanged, with no automated launch. The subsequent human
   run reports no useful improvement; custom-theme performance is not accepted.
   Canonical collection and reviewed Debug Keep are complete for this session;
   `performance-report.json`, `analysis-summary.json` and the reviewed report bind
   log SHA256 `a3bd10cdf70bbe67c125d4cae4dd12f2abe05e097ee25fb475b9f860c89fedf5`.
   Measured brush capture/composition/tint work is now small, but qualification
   remains 63.8% and basic target checks 18.9% of menu self wall time. Row callbacks
   still reach 31.4 ms and persisted opening takes approximately 1.1–1.6 s.
   No menu roots were dropped or native-helper faults recorded. The remaining
   qualification metric combines executable mapping, dynamic native-facts capture
   and owned-frame admission; current evidence does not isolate their individual
   costs. Source research found duplicate per-sample identity reads and a
   refresh-only selection scan, but neither is established as the dominant cause.
   Another speculative rewrite is not qualified by these findings. The scheduler is
   not a low-priority paint/render-frame service. Native/owning boundaries still require fresh admission;
   no target proof may cross them. Timer wakeups must not fabricate callback
   authority or drain preparation recursively. Queue-wait metrics include the
   intentional scheduling delay; the last generation has no completed close timing.
   No replacement or live attachment accompanied this analysis. A focused Debug
   follow-up now adds nested measurement of palette/Scope qualification, caller
   mapping, owned UI frames, session/identity acquisition and dynamic object facts.
   It changes no admission or native work. Independent review, production build
   and ten affected checks pass; enabled/disabled real-reader facts use exactly
   108 reads and zero native helpers each. Recorder/emitter checks verify the
   self-time partition without assigning facts capture to native-helper totals.
   Frozen Debug candidate
   `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-qualification-candidate-20261022T185457Z-f0deafa4`
   retains the DLL, exact production inputs/current tests, provenance and logs;
   DLL SHA256 is `1f531de06e3db328ea515241626f965e6046fb3da023625735830335da92d726`.
   Only scoped instrumentation/build-link inputs differ from the installed batch.
   It is not a performance fix. After explicit owner authorization and independent
   transaction review, installed that frozen DLL without rebuilding through
   `native-proxy-20261022T190204Z-7c86504b12984e2090c3991bd861de17` (now reviewed Debug Keep).
   Prior kept batch DLL/log/receipt and recovery journal remain under
   `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-opt-install-20261022T190034Z-b7858079`;
   the prior owner was canonically rolled back before exclusive preparation.
   Settings/themes are unchanged and no automated launch occurred. Its collected
   `performance-report.json` and `analysis-summary.json` isolate caller mapping:
   13383 mapping checks take 2668.6 ms self, while 53198 native-facts captures take
   84.2 ms inclusive; row/selection-owner checks take 641.9/781.0 ms self.
   No menu roots or diagnostic clock/depth/scope records were lost, and no native
   helper faults were recorded. These are differently nested wall-time components,
   not additive CPU/GPU/FPS or speedup measurements. The next Integration fix
   targets fresh allocation/resident-page queries with exact `VirtualQuery`
   fallback, never cached mapping/protection/admission. Runtime and row/selection
   execute-only policies remain distinct. Caller/core source reviews and preliminary
   focused checks pass. Reserved working-set bits are now correctly opaque;
   independent recheck and real resident acceptance pass without `VirtualQuery`.
   Own-process differential tests and independent reviews now pass. With 2000
   resident COW-image checks, the 16 MiB protection run takes 127.61 ms with the
   original query versus 2.23 ms with the new path; a 4 KiB run takes 0.91 versus
   1.75 ms. Both prove fresh owner/type/protection and zero fallback queries.
   These are informational query timings, not game/FPS evidence or an across-run
   speedup claim. The production proxy builds and all eleven affected checks pass;
   caller/core/benchmark reviews are clean. The frozen Integration candidate is retained at
   `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-mapping-candidate-20261022T194931Z-8232fbf0`
   (SHA256 `4ce5a938b46e08147e07d71705f2a69668f2b3872b9c595c49d1afd97abe0391`).
   Exact production sources/provenance, matching cache, seven current helper/theme
   tests, reports and validation are archived; all 271 inputs authenticate, with
   only the shared core check, its three caller sites and test registration changed
   from the diagnostic baseline. After explicit closed-game authorization, it was
   installed without rebuilding through session
   `native-proxy-20261022T201210Z-6917a79b3c0647e884fc7adaf5ac833c`.
   The prior kept diagnostic session was canonically rolled back before exclusive
   preparation; prior DLL/log/receipt and reviewed transaction are retained in
   `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-opt-install-20261022T201045Z-725f0264`.
   Settings/themes are unchanged and no game launch occurred. The human now reports
   FPS apparently normal, but theme application at opening remains slow. Canonical
   collection/reviewed Debug Keep are complete; per-session reports and
   `analysis-summary.json` bind log SHA256
   `7c77fd4165fcb4147baa711dd52c1ab2321d9934e62f4881929605136e794c3a`.
   Persisted openings take 1149/1343 ms with 39/51 preparation dispatches, coarse
   service 110/93 ms and queue wait 1031/1235 ms. The first Ready interval includes
   human wait before requesting a theme and is not pure opening latency.
   The human explicitly distinguishes opening from scrolling: the mapping fix,
   not the scheduler, restored qualitative FPS. Opening Integration now prepares
   and commits the finite persisted custom program within actual Ready authority,
   without artificial slice/timer pauses; this may briefly block the opening.
   Scrolling and interactive switching retain bounded slices/separation. Fresh
   admission/baselines, whole-program preflight and safe missing-receiver/failure
   handling remain required. Coarse-deadline and unnecessary changed-row-yield
   fixes remain useful for deferred work. Focused runtime/title checks pass:
   cold/warm persisted programs commit before Ready returns with zero preparation
   posts/time yields and unchanged native-operation totals; Original stays lazy,
   missing receivers defer, and unsafe/reentrant/rejected paths fail closed.
   Scrolling still yields, including exhausted-budget expansion. Production
   compilation, eleven affected checks and material independent re-review pass.
   Frozen candidate SHA256
   `3c29a641005def4f2c28a6bc8c943453f4636fd5c1f6d7c41d04df0d1f92b6a5`
   (4389888 bytes) and exact source/provenance/check logs are retained under
   `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-opening-candidate-20261022T204508Z-139dd40e`.
   All 271 production inputs authenticate; only runtime.cpp/.h and title.cpp
   differ from the preceding mapping candidate. Explicitly authorized installation
   without rebuilding completed through prepared session
   `%LOCALAPPDATA%/FF7RPianoSongs/captures/native-proxy-20261022T205547Z-cbe70daee21d4b76b3707f10eca062be`.
   Prior kept mapping installation was canonically rolled back before exclusive
   preparation; its DLL/log/receipt and reviewed recovery journal are retained at
   `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-opt-install-20261022T205433Z-8cc63316`.
   Settings/themes are unchanged and no automated game launch occurred. The human
   now reports opening roughly normal. Canonical collection/reviewed Debug Keep
   completed; `analysis-summary.json` binds log SHA256
   `a7d022c749b59bf1ba56f94810725d8033b5dee2902ed3ba6228c1b28be2a334`.
   Seven Ready-to-commit samples are 146.5–166.5 ms; row callbacks peak at 9.02 ms,
   selection callbacks at 8.58 ms, complete closes at 41.4–49.4 ms. First custom
   request after Original takes 672 ms including demanded preparation; final
   whole-theme apply still costs about 72–95 ms. No menu telemetry loss or native
   helper faults were recorded; final generation has no completed close timing.
   Remaining target-basic proofs account for about 56% of summed self wall time.
   Source audit found no slow-CPU correctness bug: slices resume work, not truncate
   admission, but indivisible work can exceed the budget. Other hardware/high-refresh
   performance is unqualified. By explicit preference, the next Integration pass
   replaces normal time-budget scheduling with finite semantic units: one explicit
   requested program or the exact changed physical row. Genuine missing readiness
   and qualified cleanup retain coalesced wake/poll fallback, not a self-post drain
   or arbitrary preparation delay. Source implementation and both focused runtime/
   title tests pass: whole requests finish within dispatch, exact-row service leaves
   unrelated progress untouched, failed/backward/frozen performance clocks preserve
   admission and native-operation totals, and fallback retains one ticket with no
   automatic successor or worker native calls. The material focus-cancellation
   correction passed final recheck. Production compilation and eleven affected
   checks passed. Frozen candidate
   `menu-theme-semantic-candidate-20261022T220503Z-697521d4` retains exact source,
   provenance, validation and build/test logs; DLL SHA256
   `8ff5052b807a9c64dd495f41c78c1970390a4785261c2fab16a35e503e8b9704`.
   All 271 production inputs authenticate; only runtime `.cpp/.h` and title `.cpp`
   differ from the previous opening candidate. After explicit closed-game
   authorization, this frozen artifact was installed without rebuilding through
   prepared session
   `native-proxy-20261022T221816Z-d3209f41850c46eca22c2d8c636e4887`.
   Reviewed install archive
   `menu-theme-opt-install-20261022T221714Z-34eeba61` retains the previous kept DLL,
   log, receipt and recovery journal; that owner was canonically rolled back before
   exclusive preparation. Settings/Themes are unchanged and no automated launch
   occurred. Physical acceptance of this new semantic-unit policy is pending;
   it is not a constant-time/frame-rate guarantee.
   Exact FPS and rendering fairness remain unmeasured. The preceding
   classifier production ZIP lacks the runtime selftest; review of
   current tests must not be described as an exact installed-test baseline comparison.
   No new code build, replacement or live attachment accompanied the log analysis.
   Wall time is not FPS/CPU/GPU measurement, and aggregate seconds from
   differently exercised runs must not be reported as a speedup percentage.
- Persistent performance instrumentation is implemented for the next focused
  Debug capture, not as a performance fix. It separates actual hook/Original and
  native helper work, ownership validation/traversal, cache outcomes, inactive
  preparation, row pending/commit/queue latency and teardown. Bounded schema-1
  windows expose inclusive/self wall times, histograms, failures and dropped
  detail; `tools/menu_theme_perf_report.py` produces a per-generation table or
  JSON. The diagnostic proxy built; affected metrics/native/palette/runtime,
  list-catalog/menu-session, actual-emitter/report and documentation checks passed.
  Independent instrumentation and installation reviews found no issues. A local
  root-plus-four-children capture benchmark observed about 0.74 us/root enabled
  versus 0.07 us disabled, excluding file logging; this is not in-game overhead
  calibration. Following explicit closed-game authorization, the metrics proxy
  was installed through prepared session
  `native-proxy-20261022T110642Z-49a0a8687dac47428f788a6b9e2d1a49`, SHA256
  `8648fbea6f688b023b9c01ad13dd0c6b49db3a5024c5f1002d27dce6b43d6f1f`.
  Exact source/provenance, prior artifact/log, reviewed transaction, focused
  validation and synthetic emitter fixture are retained under
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-metrics-20261022T110517Z-02c2056d`.
  INI and theme inventories are unchanged; no automated launch or live attachment
  occurred. The human completed the comparative Original/theme run and exited;
  canonical collection and reviewed Debug `Keep` disposition are complete.
  `performance-report.json`, `performance-report.txt`, `analysis-summary.json` and
  the reviewed capture report are retained beside that session. Across four menu
  generations, captured self wall time totals 26.65 s: callback qualification
  accounts for 18.67 s (70.0%) and basic target validation for 4.91 s (18.4%).
  Instrumented native-helper self time totals 34.6 ms. The Original-only second
  generation still incurs theme preparation and interception overhead; it is not
  a clean unmodified-game baseline. Close-hook maxima range from 162 to 443 ms,
  while native Close Original self time is about 0.05 ms. Menu generations have
  no dropped roots, clock failures, depth overflow or malformed scopes; two roots
  were dropped in unqualified generation zero. Native-helper failure counts are
  zero. Source inspection confirms seven repeated callback qualifications and
  five Subject validations on an ordinary successful LinearColor interception.
  The next optimization boundary is native-call-delimited private proof phases,
  preserving reacquisition after every reentrant/owning helper, genuine source
  alpha, Bottom semantics and current identity/authority. No new rebuild or DLL
  replacement was used for this analysis. The previous performance session was
  canonically rolled back before this preparation; instrumentation is not itself
  a performance fix or release qualification. Frame Generation was not supplied.
  ReadyToCommit and RowPendingDelay may include intentional Original residence
  or human selection delay; their largest samples are not pure opening/style
  delays. Qualification self time includes uninstrumented observation/OS/session
  work, and RAII qualifier failure fields do not count boolean rejection results.
- Human follow-up on the current performance candidate reports transparency
  apparently repaired but performance still unacceptable. The supplied recording
  and unchanged installed hash are retained with the fresh log and
  `log-summary.json` under
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-video-20261022T102409Z-964b02be`.
  Recorded Ready-to-first-apply intervals were 1.489–1.574 s; row-association
  callbacks reported up to 78 ms and restore/retirement 547–594 ms. Video frames
  also show stock-colored recycled rows while the surrounding menu is themed.
  These are not numeric FPS measurements or per-native-call profiling. The
  performance proxy session was collected as Unassessed, not accepted/Keep. Further
  work targets repeated runtime reconstruction/native validation. The video-only
  analysis did not rebuild, replace the DLL or attach to the game; the subsequent
  explicitly authorized instrumentation installation is recorded above.
- The current follow-up investigates disappearing normal-row backgrounds, scroll
  stutter and slow menu/theme preparation. The user clarified that the selected
  row remains filled while ordinary rows expose the artwork below. An immutable
  on-disk log copy and `latency-summary.json` are retained under
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-latency-20261022T124339-1b57dc80`.
  Complete applies reached 141 ms, a same-association refresh 203 ms, and nominal
  preparation slices 32 ms; these observations do not establish screenshot
  causality or bind that log to an earlier installed artifact. One explicitly
  authorized read-only live snapshot (`live-style-snapshot.json`, summarized in
  `alpha-summary.json`) found identical corresponding themed button resources but
  zero BackgroundColor source alpha on several row objects, including normal-row
  observations; neighboring sources were 0.5. This is not Slate presentation or
  a title-to-row mapping. The agent unloaded and detached successfully, without
  native game calls, hooks or writes. No game launch or DLL replacement occurred.
  Physical improvement and background restoration are not yet qualified.
- The scoped follow-up now refreshes qualified genuine non-owning palette sources
  before emission, so direct native property writes are not overwritten by a
  retained baseline. Uncertain themed properties and forwarded stale sources
  remain inadmissible; native zero alpha and owning/Bottom protocols are preserved.
  Same-association refresh removes the pre-setup non-Bottom restoring setter round
  while retaining one forced post-sync emission and truthful rollback policies.
  Callback-local direct lookup replaces nested choice scans; preparation uses QPC
  cooperative slicing. Single Subject capture uses the existing grouped seam,
  and ordinary color writes no longer use executable-page patching.
  `menu_theme_native_selftest`, `menu_theme_palette_selftest`,
  `menu_theme_runtime_selftest` and `documentation_audit` passed. Independent
  scoped reviews found no issues in the changed ownership/source/rollback paths.
  After explicit closed-game build/install authorization, the default proxy built
  and was installed through prepared session
  `native-proxy-20261022T101756Z-6b936afa9e8e4aa696d394a0accbd003`.
  Candidate SHA256 is
  `8bd26fb75d6192781d6e25802bfc2266cceff1a2cafca98a1d41c3f181d2cf25`.
  Exact production sources, provenance, prior proxy/log/receipt and reviewed
  installation journal are retained under
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-perf-20261022T101651Z-1d8eccfe`.
  The prior kept preview-volume session was canonically rolled back before new
  preparation; current preview-volume sources remain in the new build. INI and
  all active theme files were verified unchanged. No automated launch occurred;
  physical background/latency and preview-volume compatibility remain pending,
  with collection and disposition deferred until the focused human run.
- Preview color-only roles (`Preview/Background`, `Border`, `Accent`, `Track`,
  `Text`) are implemented in source with independent defaults and committed-theme,
  generation-bound publication. Parser/renderer/fade focused tests pass; the review
  identified and corrected a retired-owner fallback which could otherwise hide a
  later preview badge. The combined proxy build and all 15 affected tests pass;
  physical appearance remains pending. No audio-content or Preview PNG
  replacement is claimed.
- Current-pointer research and an authorized read-only default-profile snapshot
  identified the separate `Item_Cell_Light_C` owner and its `Cursor_Left`,
  `Img_BtnLight` and `HoverBase` images. Current cursor/effect ownership metadata is
  independently recovered for both profiles in
  `analysis/MenuThemeCursor-20261022.json`; older evidence is import-only. The
  observed native effect name is None, not permission to replace arbitrary effects.
  Cursor owner admission is implemented; its focused native test and scoped
  independent review pass. `Cursor/` plan parsing also passes focused tests.
  Full runtime wiring is implemented and its scoped review has no findings;
  the combined proxy builds and focused Cursor/Preview runtime checks pass.
  The complete examples pass real WIC/plan validation. Source examples now add
  distinct Steins;Gate brass/nixie and Evangelion purple/green cursors, soft glows
  and Preview palettes while preserving all previous design bytes/selectors.
   These additions were installed with the owner-authorized, closed-game existing
   default-profile DLL; no further rebuild or interference with parallel work.
   Installed SHA256 is `be598d2e44832f60ccc815eb929913bd1a2b4bdb8f75668e0e057bf530a54cff`,
   prepared session `native-proxy-20261022T091930Z-1f4161632d594d0d96aae1f21e445ce2`
   under `%LOCALAPPDATA%/FF7RPianoSongs/captures`. The admitted artifact/provenance,
   available source and explicit subsequent source differences are retained in
   `menu-cursor-preview-20261022T091303Z-e7e56a1f`; publication/recovery journals are
   in `menu-cursor-preview-retry-20261022T091901Z-f4567e0d`. Prior DLL/themes are
   retained; existing design, INI, debug-cells and unrelated work are preserved.
   No tooling launch or physical Cursor/Preview appearance qualification is claimed.
- Added the separate Russian [user guide](../ThemesGuide.md), covering installation,
  controls, authoring, practical image geometry, format/resource limits and recovery
  without native implementation details. Packaging stages it as `ThemesGuide.md`
  beside two full-menu `Themes/steins-gate` and `Themes/evangelion` samples,
  which replaced the earlier ocean/orchid examples on 2026-10-21. Both pass real
  PNG/WIC decoding and plan compilation; documentation parity and fake-package
  staging/inventory tests pass. Both reached `applied_complete_program` and were
  visually reviewed in game on 2026-10-21 after Box textures were reduced to
  native 1 px/unit scale (see [Native Brush Geometry](MenuThemes.md#native-brush-geometry)).
  This is not a published release.
- Added worker-side PNG decoding into owned BGRA8 pixels, color overrides,
  bounded validation, selected-ID configuration read/write and a reserved cycling
  shortcut setting. The development contract is in [Menu Themes](MenuThemes.md).
- Added a native texture bridge with rooted brush ownership, bounded
  process-lifetime retention, fresh-mip upload checks and fail-closed cleanup.
  It also has typed image lookup, positive receiver/target and forward WidgetTree
  checks, rooted full-original-brush capture and retained-brush apply/restore.
  Focused tests cover exact restoration, stale/recycled/replaced owners and
  nested entry. Independent review prompted post-factory/resource/holder checks
  and suppression of native cleanup after compromised retention, even before
  holder binding; corrected focused tests and proxy build pass.
  Owning Ready/input/close scopes connect cached switching and success-only
  selection persistence. The earlier background slice established visual keyboard
  switching; the installed development candidate now contains the full-menu source
  composition described below. Full-menu work adds
  a tested offline selector/palette compiler and reviewed multi-image composition,
  including null originals, exact snapshot sharing and target-bound restoration.
  Native adapters now cover images, pie/tiled images, indexed image sets and button
  states, with typed text/panel subjects and current-row ownership checks. Animated
  palette hooks retain latest untinted owning values, revoke retired contexts and
  reject stale sources after overlapping forwarded updates. These foundations pass
  their focused tests and scoped independent reviews; the compiler also passes a
  real INI/PNG/WIC-to-plan integration test. Full runtime/lifecycle composition is
  implemented: main/song/Speed brushes, tints and palettes, semantic row states,
  selective recycling, whole-program admission, qualified rollback and successful
  selection persistence. The proxy builds and all 13 affected selftests pass;
  scoped independent review found no remaining concrete defect in that batch.
  The first full-menu run subsequently exposed rejected resource reservations and
  a blocking preparation interval (see collected evidence below), so full-menu
  native execution is not manually qualified. A bounded stack-local
  strong panel-slot containment checker and actual native ancestry reads are now
  implemented and independently reviewed against the retained both-profile
  evidence. Background-only visual acceptance and the later controller candidate
  are distinguished below; full-menu source tests do not qualify native rendering.
- Independently recovered older-build bridge addresses are integrated into the
  canonical catalog and generated trees. Older evidence is static-import-only;
  disk equality and runtime behavior are not qualified.
- Added a read-only observer around the existing exact menu-ready notification.
  It copies session, native thread/TLS, GC, exit-purge and transient-package facts;
  stable observations are not allocation authority. Both profiles have independently
  recovered noncallable verifiers; the observer itself performs no native construction
  or UI mutation.
- The seven focused config, theme, native-bridge, observation, runtime, menu-session
  and title-input tests
  pass for the default profile; the native proxy builds. Catalog generation/check,
  focused catalog regressions and installer fake tests pass. Independent review
  prompted authority-drift fixes and found no remaining scoped implementation,
  observation, catalog or installation-plan defects.
- With owner authorization and both game processes closed, previously installed the diagnostic
  proxy through the canonical session workflow. Artifact SHA256:
  `12f250965bf526731ecfe22b93b138a8d17fbaca7c409cf724d060841cf7be28`.
  The session is `native-proxy-20261020T184334Z-e3512b2bf1fb4bd9af9131724de5edc8`
  under `%LOCALAPPDATA%/FF7RPianoSongs/captures`; the previous DLL/log/receipt and
  current source/provenance are preserved in
  `menu-theme-observation-20261020T184201Z-630e3b8c`. No game was launched by tooling.
  The owner completed the focused open/close/immediate-reopen/selection scenario.
  Collection has two stable `observed` events for successive Ready generations,
  matching native/current threads, TLS role 2 and clear GC/ExitPurge flags.
  Exact close restoration and subsequent selection changes are logged; no observer
  failure/drift or error/fatal marker was found. Raw names remain unverified and
  package/class zero serials are diagnostic-only, not lifetime authority.
  The bounded review/report and complete log are retained in the session, finalized
  as `kept` before its later canonical replacement; this is read-only default-profile
  development evidence only.
- After the owner's separate mutation authorization, canonically replaced that
  diagnostic with background-only proxy SHA256
  `b2d3f6553008147a87e76984a78a407e17a7ff13390d734fe2f89bdca9173a4f`.
  Session `native-proxy-20261020T202500Z-ad9a03aaad42437a859ecbd2cecc562e`
  was collected after the owner reported no visible response to Ctrl+F6.
  Both fixtures prepared and callback/lookup/original capture progressed, but
  native preparation failed closed at `fresh_mip` (`FreshLayout`) before upload
  or background application. No successful apply/persistence event was logged.
  The archived log and failure assessment are retained. Independent static research
  found an incorrect capacity-equals-count assumption: native first growth reserves
  at least four pointer slots for one mip, then allocator-quantizes capacity. The
  failed run did not log its actual capacity or establish that no other mismatch exists.
  Source/provenance, previous DLL/log/receipt,
  unchanged INI backup and installation journal are retained in
  `menu-background-20261020T202002Z-e182878a` under the same captures root.
  Two hash-checked test themes were published exclusively into the previously
  absent `Themes` directory; the existing INI was not modified. Tooling did not
  launch the game.
- Corrected both fresh-mip capacity checks without weakening the one-mip, dimensions,
  format, byte-count, flags, lock or pointer checks. Only array element zero is read;
  capacity is not an iteration/allocation bound and must remain stable across
  materialization. Added copied header/first-failure diagnostics and faithful spare-slot
  fixtures. Independent scoped review found no defects; seven focused tests and the
  proxy build pass.
  With renewed owner authorization and closed game processes, canonically rolled
  back the failed session and installed corrected proxy SHA256
  `7542d4f83b238fe4a3ee31afbe80dd1b93a2bbcfccfb92e946ca18a8e0480e23`.
  Session `native-proxy-20261020T210250Z-a1ba61db932149a3a85491c2055e2eac`
  was prepared for a repeated human check. Failure evidence, both source/provenance
  identities, previous DLL and diagnostic recovery receipt are retained in
  `menu-background-capacity-fix-20261020T210220Z-10af073f`. INI and all four fixture
  files were hash-verified unchanged; tooling did not launch the game.
  The owner repeated Ctrl+F6 without a visible change. The corrected session is now
  collected: native preparation reaches `cached_original_captured` with qualified
  ownership, but there is no apply or persistence event. The same log records
  preview requests, consistent with the owner's working Ctrl+F7 comparison.
  `background-input-assessment.json` retains this narrower result. It does not prove
  which PlayerInput filter suppressed the theme gesture or qualify rendered output.
  Theme sampling now uses the existing preview/input poller, with one generation-bound
  ticket delivered to an owned game-thread message callback; native work never runs
  on the poller. Held keys, focus loss (including notification-only loss/regain), stale
  owners and failed posting cannot replay an old request. Scoped independent review
  found no remaining defects; seven focused tests, the proxy build and catalog checks pass.
  After the owner's authorization and fresh closed-process/ownership checks, canonically
  replaced the collected candidate with SHA256
  `3d722ff2495a70d285fc827b5f6e7511ecff8f402a3d43c9700f4da5ef9280ab`.
  Session `native-proxy-20261020T213042Z-c8c4223d49a74649943aeb757e7f7967`
  was collected and finalized `kept` after the owner confirmed keyboard switching and
  supplied screenshots of both contrasting fixture backgrounds. The exact log records
  qualified apply and saved events for `probe-blue`, `probe-magenta` and `original`,
  without error/fatal/compromised-ownership markers. Session-local
  `background-switch-review.txt` and `background-switch-assessment.json` retain this
  scoped default-build acceptance. Source/provenance, previous DLL,
  failure log/assessment, receipt and recovery evidence are retained in
  `menu-theme-key-polling-20261020T212716Z-75c0ab68`. INI and all four theme fixture
  files were hash-verified unchanged at installation. Tooling did not launch the game.
- Gamepad previous/next support is implemented using the approved gesture: hold L3,
  tap L2 for previous or R2 for next; both triggers together suppress switching
  until neutral. XInput and the existing native digital-key fallback form chords
  within their own provider. Keyboard disable does not disable gamepad input;
  keyboard next, preview and autoplay remain unchanged. Eight affected tests and
  the proxy build pass; independent comparison with the accepted keyboard source
  found no issues. These offline checks do not qualify physical controllers.
- The prior owner-authorized closed-game controller build had SHA256
  `052d6dde8497dc27c3bc7fcef8822852798094ec4e388d7ae94341db3d2833ea`.
  Session `native-proxy-20261020T215117Z-8ad9c02fef4d421386c2ff507440fa8f`
  was canonically rolled back for the full-menu replacement below. The accepted keyboard session was canonically rolled back only
  to replace it; its manual acceptance, exact DLL and recovery receipt remain in
  `menu-theme-gamepad-20261020T215014Z-a6cf6053` under the captures directory.
  The new exact source/provenance/install journal is retained there too. INI and
  all four fixture files are unchanged; tooling did not launch the game.
- Installed the reviewed full-menu development proxy after fresh closed-process,
  managed-ownership and artifact checks: SHA256
  `839855fc9d4bafb1c8f4bb2fda9b5141c8d07e201ee69fda5a7909289c6631aa`.
  Session `native-proxy-20261021T104022Z-78790848194047398e90a7f673f63914`
  was collected after the owner's run and canonically rolled back for the
  preparation correction below. Tooling did not launch the game.
  Exact source/provenance, prior DLL,
  receipt, INI, fixture hashes and transition journal are retained under
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-themes-full-20261021T103214Z-3696bdef`.
  Added `Themes/full-ocean` and `Themes/full-orchid` through exclusive same-volume
  publication into absent names; existing themes and INI were hash-verified unchanged.
  The new fixtures cover main styling, song/button states, SpeedIcon entries and
  palettes. The run rejected `full-orchid` during startup reservation and
  `full-ocean` during expanded runtime reservation; only `probe-blue` and
  `original` were applied/saved. It therefore does not establish full-theme
  rendering despite the two reported appearances. Opening preparation occupied
  approximately 4.6 seconds of the observed log interval. Session-local
  `full-theme-budget-and-latency-assessment.json` retains the exact failure
  evidence. The correction uses actual-resource accounting, incremental qualified
  game-thread work and immutable-authentication reuse; dynamic
  session/thread/GC/package checks and original availability remain mandatory.
  The owner explicitly rejects arbitrary theme/brush/reopen quotas, rather than
  accepting a larger fixed count. Dynamic retained-resource and palette-source
  bookkeeping now uses actual byte/allocation checks; speculative
  future-open reservations are removed. The file/plan layer no longer imposes a
  separate entry-count quota: manifest bytes, image dimensions, decoded bytes,
  syntax and native ownership validation still bound the actual resources.
  Dynamic palette-source storage now passes focused growth/allocation-failure,
  forwarding/concurrency and ownership tests, with scoped review clear. File and
  plan tests likewise accept manifests beyond the former entry-count ceiling.
  Native retained records and row associations now also use stable dynamic
  storage with byte/allocation admission; focused native, palette, file, plan and
  observation tests pass. Scoped native review found only a portable test
  assertion about an empty deque, corrected without weakening production checks.
  Runtime preparation/bookkeeping integration is implemented and its focused
  runtime/title-input tests pass: dynamic contexts, incremental owned-dispatch
  preparation, immediate Original and deferred success-only persistence. The
  common proxy build and all 13 affected tests pass, with final scoped recheck
  clear; these checks do not qualify physical rendering or measured latency.
  Facts-only authority checks now reuse authenticated immutable main-executable
  contracts; dynamic session, identity-service, thread/TLS, GC/purge and package
  facts are still read/rechecked on every capture. The ordinary open observer
  remains independently authenticated. Focused reader/concurrency tests and scoped
  review pass; no measured in-game timing improvement is claimed yet.
- With owner authorization and both game processes closed, installed the reviewed
  preparation correction: SHA256
  `b0c9303fcc25fa0036758dd98798ccabe5a29c5179267d4bc90a29248d2cc9f6`.
  Session `native-proxy-20261021T124804Z-74a562e1038d46c1abf2a545b8a2ccf9`
   was collected after the owner's focused run and canonically rolled back for the
   cycling correction. Exact candidate/source/provenance, prior DLL/receipt/log,
  accepted keyboard recovery receipt, INI/fixture backups and transition journal
  are retained in `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-themes-preparation-fix-20261021T123813Z-fed9aef3`.
  Canonical rollback/prepare and final artifact/disabled-ASI checks passed;
  INI and every theme file remain unchanged. No tooling launch occurred.
   The owner reports no apparent opening stall, but two invisible cycling steps.
   Collection confirms four custom candidates: obsolete probe fixtures were still
   active alongside both full themes. Both full IDs repeatedly defer without any
   ready/apply event; only probe backgrounds and Original apply. This is not full
   theme acceptance. Session-local `theme-cycle-assessment.json` retains the exact
   log/artifact and inventory evidence. No replacement occurred during diagnosis.
- Corrected paired metadata preparation to retain progress without a separate
  publication yield; cycling now includes only Original and fully ready programs,
  without input changing preparation priority. Full-fixture/interleaved-input
  regressions, the proxy build and all 13 affected tests pass; scoped review is clear.
  Installed SHA256 `300a72cf05a50343b73b49827d4803f85fe6a5c7486948bba98a99a7b5e242f1`
  in session `native-proxy-20261021T133329Z-a88465f8ed2046e18d94cf2ba82534b9`,
  collected after the owner's visual check and subsequently canonically rolled back
  for the reviewed visual correction below.
  Evidence and prior/recovery artifacts are in
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-cycle-fix-20261021T133010Z-7f8b0168`.
  Hash-verified owned probes were non-clobber renamed outside active Themes,
  with additional external backups; only `full-ocean` and `full-orchid` remain
  active. INI and full-theme files are unchanged. Closed-game canonical transition
  and final verification passed; no tooling launch. Both full IDs subsequently
  reached ready/applied/saved and the owner supplied screenshots of both palettes.
  However, first readiness was delayed, Original row text changed during preparation,
  selection backgrounds were inconsistent, and the sample palettes omitted several
  static labels. Session-local `full-theme-visual-assessment.json` preserves this
  partial result. No replacement occurred during diagnosis.
  Disabled palette registration now captures sources without invoking native setters;
  unchanged disabled policies also remain passive unless restoring an applied override.
  Focused owning/source regressions and scoped independent review pass. Runtime
  selection/preparation corrections now use cooperative work slices, one cancellable
  early request and an Original fallback when recycling invalidates a complete
  program. Exact BottomColor restoration now preserves both the owning UObject
  color and the complete Slate attribute, including unset/delegated state and latest
  genuine native updates. The common proxy build and all 13 affected tests pass;
  scoped runtime/palette/fixture and installation-plan reviews report no findings.
  Static research confirms BottomColor activates a separate set/unset Slate attribute;
  color-only restoration was insufficient. Updated sample
  palettes cover information/score labels and avoid multiplying normal button textures
  by an accent tint.
- Installed the reviewed visual correction through the authorized closed-game
  canonical transition: SHA256
  `b4787323c6c2377da581e9d5e6fe7c3f3a9f4ab99d4967d747417c76535957f8`,
   session `native-proxy-20261021T143327Z-3e98bf5f3ce14cbf9cc00fd4b22c6f2e`, now collected.
  Source/provenance, prior DLL/receipt/log/assessment, INI and old/new fixtures,
  known-working recovery receipt and transition journals are retained under
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-visual-fix-20261021T140011Z-9fd239d1`.
  Revised full-ocean/full-orchid directories were exclusively staged and published;
  previous directories remain in the recorded same-volume archive outside Themes.
   INI was unchanged at installation; no tooling launch. The next owner run reports
   delayed opening, close/scroll stalls, focus-dependent loss/reapplication, weak
   speed artwork and the unchanged piano silhouette. The collected log contains
   eight whole-menu Original fallbacks during row rebinds and 107 complete-program
   application events. Session-local `lifecycle-seven-issue-assessment.json` records
   the evidence and distinguishes the reported stock highlight from a proven
   regression. Row-local reconciliation and unchanged-state fast paths are now
   implemented; the fixture manifests also omitted `MusicList/BaseImg`. No new
   binary or asset replacement has occurred during this diagnosis. Physical
   restoration, responsiveness and complete customization remain unqualified.
   External fixture drafts now explicitly replace that artwork, neutralize button
   brush tints and distinguish all three native speed states with tightly bounded
   artwork. Native geometry/index policy stays unchanged; draft assets are not
   installed or visually qualified. The reported Original selected-row difference
   still requires comparison with native highlight behavior, not dismissal from
   a screenshot alone.
   Routine row rebinds now keep unaffected subjects styled, semantic changes avoid
   redundant brush application, and unfocused maintenance is separated from
   cancelled input intent. Close avoids duplicate palette restoration; warm opens
   reuse immutable resources through a qualified Ready slice. The first common
   build passed 13 affected tests. Independent review then found a newly discovered
   Speed member could finish preparation without reapplication; a non-saving
   maintenance intent and focused regression now address that path. Final scoped
   recheck found no issues, the proxy builds and all 13 affected tests pass; no
   measured native latency claim follows.
   The reviewed correction is installed as SHA-256
   `0b62692ce5927a41f096cbe4099955db353e3875cf789e4ea1279d136ea07a6d`,
    session `native-proxy-20261021T153928Z-3a6f8b5b4444440c99bcc285de7bdada`, now
    collected after the owner's next run.
   Source/provenance, prior DLL/log/receipt, INI, old/new themes, recovery receipt
   and publication journals are retained under
   `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-row-local-fix-20261021T153253Z-d8f8e530`.
   The previous collected session was canonically rolled back. Revised themes add
   BaseImg artwork, neutral button-state tints and three distinct Speed entries;
   previous directories are archived outside active Themes. INI is unchanged and
    tooling did not launch the game. The next run still shows mixed stock/custom
    rows during fast switching, retained Original row tint, incomplete high-score
    and rank effects/decor, weak Speed art and delayed opening. Logged custom-theme
    close restoration takes 937–1297 ms. Session-local
    `rapid-switch-close-visual-assessment.json` preserves the failed acceptance;
     transition coherence and repeated validation/restoration work are under
     correction. Same-widget read-only Ocean/Original comparisons confirm residual
     accent RGB in 10 of 13 Original `Pnl_Light` instances. Native animation reads
     current UObject colors; separating untinted source properties from themed Slate
     presentation is implemented in source without RGB-equality heuristics;
     common proxy build, 13 affected tests and scoped write-back review pass. Research is
     retained in `analysis/MenuThemeColorFeedback-20261021.json`.
     The owner-authorized visible-record snapshot identifies `MusicList/Deco` under
     `Pnl_HighScore`, using `U_Result_WinDeco`, with a 55-by-60 brush. Speed entries
     have 18-by-11 authored brushes and distinct native black/blue/white tints.
     External fixture drafts now replace Deco, neutralize indexed Speed tints,
     remove dotted decoration and cover actual text outline/shadow layers without
     activating additional Bottom attributes. Batched Button/ImageSet transactions
     and one-pass palette retirement are integrated in source. Native/runtime
     batching and source write-back regressions pass, including irreversible-fault
     handling and separate UObject/Slate ownership. Revised fixtures have consistent
     final inventories. The authorized closed-game replacement is installed:
     SHA-256 `61b6ad3a159cbb12a305c805a5c95ec5d5097e12cc931c557593dd4983d65acc`,
      session `native-proxy-20261021T165906Z-6b0ffd7de3384d3a9a7d444819a4307a`,
      collected after the owner's video run and subsequently canonically rolled back.
     Source/provenance, prior DLL/receipt/log/INI/themes, live read-only observations,
     candidate/recovery receipts and transition journals are retained under
     `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-batch-feedback-20261021T165222Z-91797e30`.
     The collected prior session was canonically rolled back. Revised Ocean/Orchid
     fixtures were exclusively staged and published; old directories are retained
     outside active Themes. INI is unchanged; tooling did not launch the game.
      The owner's 30-fps video confirms a record-decoration strike-through and
      nearly illegible custom speed arrows. The replacement Deco has an opaque
      center seam stretched by the retained half-margin Box brush; arrow image
      replacements retain black native widget color. External-only drafts clear
      the Deco seam and preserve native resource-less Pie arrow geometry with
      explicit theme colors; rounded Speed artwork keeps authored 18-by-11 sizing.
      The collected log records Ready-to-saved-theme delays of 858/782 ms and
      close restoration of 266/281 ms. Warm-open reconstruction is under focused
      analysis. `video-geometry-assessment.json` retains the exact video/log evidence;
      independent visual review found transient highlight/high-score differences
      but does not establish their cause or disprove the owner's unrecorded glitch.
      Current-generation Subject sharing, verified class hints and retained worklists
      now reduce duplicate warm reconstruction; current identity/containment and
      native baseline checks remain mandatory. The proxy builds, all 13 affected
      tests pass and scoped source/install reviews found no outstanding defects.
      The authorized closed-game correction is installed as SHA-256
      `b51052c068b0a927ed6e3ce43c77bd4390d103ac1ebac01ff1d3aef52ace0143`,
       session `native-proxy-20261021T175943Z-7e873aa84e544b11844e81537fd3ba2c`,
        collected after the owner's new NVIDIA recording and subsequently canonically
        rolled back for the reviewed refresh/capture correction below.
      Exact source/provenance, prior DLL/receipt/log/INI/themes, revised fixtures,
      recovery receipt and transaction journals are retained in
      `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-video-fix-20261021T175623Z-5aa13d31`.
      Deco's midpoint gutters are transparent, arrows retain native Pie geometry
      with palette-only colors and Speed entries use rounded 18-by-11 artwork.
      Old theme directories were non-clobber archived outside active Themes; INI
       is unchanged and tooling did not launch the game. The owner reports rapid
       selection artifacts in the new 60-fps recording; independent frame review
       and row-refresh diagnosis are in progress. This run still logs 805/715 ms
       from Ready to first custom application, 313 ms close restoration and 96
       row retirements with repeated preparation. These spans include scheduling,
       not isolated native-operation timings, and do not alone identify the visual
       cause. `nvidia-rapid-selection-assessment.json` retains the exact video/log
       identity and events. No replacement during diagnosis or physical complete-fix
       claim is made; prior passing offline checks did not resolve these reports.
       Source inspection found that every setup invocation unconditionally retires
       an existing row association before distinguishing a same-element refresh
        from a genuine pooled-row rebind. The guarded refresh path now retains
        verified current associations and captures fresh post-original native
        baselines without waiting for general admission. Proxy compilation, 13
         affected tests and scoped independent review pass; it is included in the
         reviewed development replacement below.
        The log lacks old row identities and does not prove how often this specific
        case caused the recorded artifact. Warm-open reconstruction remains under
        investigation; these checks do not establish physical flicker/latency fixes.
        Read-only analysis identified repeated uninterrupted containment checks
        during warm Button-member original capture and passive palette binding.
         Copied per-generation service/queue measurements and removal of the
         redundant passive-bind validation pass their focused tests and scoped
         review. Grouped original-member capture is implemented in the native
         adapter; its focused native test and scoped independent review pass.
          Runtime grouping is integrated, including fresh same-association baselines
          and separate API/member/progress counters. The proxy and all 13 affected
          tests pass; scoped source reviews found no outstanding defects.
         No larger blocking quantum or cached authority replaces current admission;
         no in-game timing improvement is claimed from these offline checks.
       Independent inspection of the new recording confirms stock row presentation
       lasting about 0.05–0.48 s within a custom menu, about 0.72 s of stock styling
       on reopen and a two-frame stock flash before close. Single-frame warped or
       doubled text also occurs, but its rendering/mod cause is not established.
       Current arrows are colored/full-shaped and the visible record no longer has
       the previous strike-through. Exact frame identities/times are retained in
        `nvidia-independent-visual-review.json` beside the collected log; these visual
        findings are not a complete runtime acceptance or proof of all causes.
        The owner confirmed DLSS Frame Generation was enabled and deprioritized
        single-frame UI deformation; multi-frame stock rows remain a distinct issue.
        The authorized closed-game refresh/capture replacement is installed as
        SHA-256 `be0ca46b18a31b9ee0b9eaadd679b76166d2f966ce432dc186a3bc7b15637b7b`,
        prepared session `native-proxy-20261021T195637Z-7401b0a0d2554768b32011de2c81973b`.
        Exact tested sources/provenance, prior DLL/receipt/log/INI/all theme files,
        known-working recovery receipt and transaction journals are retained in
        `%LOCALAPPDATA%/FF7RPianoSongs/captures/menu-theme-warm-capture-20261021T195508Z-62ccbe1a`.
        INI and existing themes were hash-verified unchanged; packaged example themes
        were not added to the active inventory. Tooling did not launch the game.
        Same-row refresh, rapid-scroll presentation and actual warm-open/close timing
        remain pending owner validation; new copied metrics do not imply a speedup.
     No live mutation occurred during the read-only diagnosis. Offline tests did not establish
    native Original equivalence, responsiveness or complete visual coverage.
- Keyboard-triggered background replacement is visually confirmed, not full theming.
  Close/reopen restoration, restart/reapply persistence, GC survival, gamepad and
  older-profile runtime acceptance and release qualification remain pending.

## 0.3.1 release disposition (2026-10-20)

- This patch redesigns only the startup song-cache overlay and its progress
  projection: an animated chocobo carrying Cloud, checked/ready/skipped counts,
  rebuilding-time estimates and a fading ready notice. Packaging remains the
  standalone DLL with the transcription model and separate game-build archives.
- The owner explicitly accepts missing fresh visual checks of the new overlay,
  exact-ZIP gameplay and the full per-build manual matrix for **0.3.1**. Prior
  development reports and the 0.3.0 disposition do not qualify these new artifacts.
- Automated Release checks, independent review, clean source identity and
  transactional packaging remain required. This decision grants no installation,
  launch or publication authority; no new in-game qualification is claimed.

## 0.3.0 release disposition (2026-10-20)

- The release packages use the standalone proxy DLL, with separate archives for
  the game builds declared in `release.json`. The alternative ASI is not bundled.
  Package identity, checksums and build provenance belong to the generated
  release sidecars; no publication or new game installation is implied.
- The product owner reports having tested the current development version:
  "everything works." No exact artifact hash, per-build scope, input-device
  inventory or completed scenario matrix was supplied with that report. It
  supersedes pending user feedback, not the historical logs or their bounded scope.
- The owner explicitly accepts the absence of fresh exact-ZIP gameplay and full
  per-build manual qualification for **0.3.0**. This includes older-build startup
  and gameplay, preview alignment/audio and native-controller controls, autoplay
  record preservation, and the complete native audio/chart transition matrix.
  Automated checks and the general positive report do not establish those claims.
- The earlier same-song replay/profile-change rejection below has no independently
  attributed fix or exact-artifact reproduction result. Retain it as a historical
  unresolved report, not a newly demonstrated release failure or a verified fix.
  The positive user report does not establish a targeted regression pass.
- This version-scoped manual-coverage decision does not waive automated build,
  test, audit, provenance or transactional package failures. Canonical Release
  validation is still required before a new archive is claimed complete.

## Older-build preview anchor (2026-10-20)

- Independently recovered the preview geometry verifiers, root globals and getter
  RIP references from the imported 1.004 program. Imported PE identity matches the
  catalog; the consumed widget, controller, geometry, parent and root layouts and
  coordinate conversion agree with the current reader. Exact per-build values
  and static evidence remain in the canonical RVA catalog.
- All entries remain optional, without hooks or native calls. Missing or
  mismatching gates, failed reads and inadmissible layouts hide only the badge;
  geometry admission is not a preview-audio, menu or gameplay prerequisite.
- Catalog generation/check, proxy compilation and focused anchor, preview-runtime
  and startup-overlay tests passed for both declared builds; source-document
  audits passed too. Independent review found no concrete defects and confirmed
  unchanged default-build mappings and optional-failure isolation. The build was
  returned to the default catalog. No fresh full imported-file hash, older live
  run or visual qualification is claimed. The installed proxy is unchanged.

## Preview volume integration (2026-10-22)

- Preview PCM previously bypassed both the game's master/music Config gain and
  the retained piano asset/bank attenuation. Playback now applies their product
  once before queueing PCM; fixed factors come from the export/scaffold bytes,
  not a user-facing preview-volume setting or cache normalization change.
- Both supported catalogs qualify bounded OS-copy settings projections. A valid
  zero mutes preview; unavailable, changing or malformed state denies only that
  attempt, without calling native audio functions from the input thread or
  disabling future attempts. Sampling uses the configured target at each start,
  not every transient native envelope or voice effect.
- Authorized read-only Frida observations on the default build confirmed
  Master/Music 10/5, 5/5 and 5/0 produce Config gains 0.5, 0.25 and 0. No gain or
  playback mutation was made. See [recovery and live evidence](../analysis/PreviewGameVolume-20261022.md).
- Focused core PCM, runtime admission, settings projection, native export,
  documentation and catalog checks pass. Settings/runtime tests also pass after
  selecting the older catalog, then restoring the default catalog. The default
  proxy builds; independent scoped safety review found no concrete blockers.
- With explicit authorization and both processes closed, installed the reviewed
  default proxy through session
  `native-proxy-20261022T092309Z-a41b7d6fd7794006b4acc67c9ac23626`.
  Prior proxy bytes/receipt and candidate provenance are retained under
  `preview-volume-20261022T092234Z-ddafdb56`; the previous session was rolled back
  through its journal before preparing this one. It had no fresh log to collect.
  INI files, songs, caches and themes were not changed by installation.
- The owner confirmed comparable preview/gameplay loudness and silence at Music0,
  then complete normal exit. Collection and reviewed `Keep` completed for this
  candidate; the archived log contains preview start/navigation/menu-close events
  and no error/fatal records. The exact final comparison slider pair was not
  separately supplied. The corrected DLL remains installed with prior bytes and
  the manual report retained. Older-build live behavior and exact parity of every
  dynamic mix effect remain unverified; no release qualification is claimed.

## Song preview development (2026-10-19)

- The preview badge was redesigned (navy panel, time or control hint, whole-song
  timeline with the excerpt window, a 16-band equalizer while playing, and a
  contrast-stretched waveform when idle). Row changes and the manual toggle now
  fade out briefly instead of cutting, with a 400 ms hard-stop fallback. An
  independent review found no blockers; full ctest passes. Installed through
  prepared session `native-proxy-20261019T183459Z-73f804efd5044c1b818b5f44c29fdab8`
  (prior session rolled back); in-game listening and visual checks are pending.
- Autoplay L3+R3 and preview R3 were XInput-only, so a Bluetooth DualSense without
  XInput emulation could not trigger them. Native PlayerInput stick clicks are now
  merged in as event-backed facts cleared on focus/application loss, device
  removal and teardown.
- The badge now appears only while a preview loads or plays, fades in and out over
  150 ms, and its clock shows the real song position and length instead of the
  excerpt clock. Idle waveform and control hint were removed. The excerpt
  `waveform` remains in the runtime cache codec but is no longer drawn. Reviewed
  (no blockers) and installed through prepared session
  `native-proxy-20261019T192442Z-9756db1d00154c76989403456cc107fc` (prior session
  rolled back); in-game DualSense L3/R3, fade and clock checks are pending.
- Integration work is in progress for custom-song previews: automatic after a
  selection dwell by default, with manual and disabled modes in the INI.
  Excerpts and waveform metadata are prepared offline; preview playback must
  remain independent of native gameplay audio, charts and scores.
- Prepared excerpts are bounded PCM artifacts with sound-derived waveform data,
  validated on cold and warm paths. Cache identity v49/runtime format 17 requires
  rebuilding earlier song caches; gameplay audio generation is unchanged.
- Authenticated read-only live observation and foreground scrolling correlated
  the selected row with the game's root-overlay coordinate conversion. The
  catalog-gated badge follows that row on the default build; absent or mismatching
  gates and unqualified layouts hide it rather than using guessed coordinates.
- Focused pipeline, descriptor, configuration, policy, audio, renderer, anchor,
  input/startup and native compatibility tests pass, as do the source-document
  audit and diff check. The production proxy built successfully without WinMM
  imports. Independent reviews found no remaining concrete blockers in preview
  lifetime, borrowed geometry reads or the combined worktree's native-cache
  changes. A documented WinMM position-unit fallback was corrected and tested.
- After explicit human authorization and verified game/bootstrap closure, the
  prior owned proxy session was rolled back and the reviewed preview proxy was
  installed through prepared session
  `native-proxy-20261019T175733Z-37d18ef68309478d892d03facf1d12ce`.
  Both DLLs, receipts, production-input hashes, checks and prior-version recovery
  plan remain under
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/song-preview-integration-20261019T175536Z-857aba5f`.
  Installed bytes match the reviewed candidate; settings, songs and scores were
  not modified. Actual-device listening, drawn scrolling/close/reopen/focus
  checks and post-exit collection remain pending. Preparation is not a gameplay
  or Release pass; the external-fixture native warm-cache benchmark was not run.

## Autoplay development (2026-10-19)

- The Integration candidate implements a seated-only toggle with configurable
  Ctrl+F-key input, same-XInput-controller stick clicks, distinct on/off sounds,
  and no native or custom personal-record replacement after any assisted play.
  Native note timing and actual score/rank calculation are retained for stock
  and custom songs. Its first focused run failed: custom songs were absent
  from the list. That candidate was rolled back; the corrected candidate later
  passed the bounded human check below, not full gameplay or release qualification.
- Independent scoped reviews found no blockers in the marker lifetime, copied
  input authority, nested/concurrent result shadows, optional-hook degradation,
  sound ownership or older-build catalog additions. The sound helper resolves
  the genuine System32 WinMM API without importing the supported proxy basename.
  Focused tests cover these policies and failure paths; actual device audibility,
  automatic notes and personal-record preservation still need human validation.
- Both catalog builds have statically recovered optional autoplay contracts;
  [1.004 evidence](../analysis/AutoplayRuntimeEvidence-1.004.md) is independently
  recovered, PE/signature-correlated import evidence, not an authenticated older
  disk or gameplay run. ASI/proxy builds and the nine selected tests pass for
  both catalog builds; the default build is restored in the build directory.
  Final DLL import inspection confirms no WinMM/waveOut imports in either
  default-build product. Complete
  optional-hook rollback leaves the rest of the product functional; retained
  hook ownership still fails closed. This is focused Integration evidence,
  not release qualification.
- Static contracts are retained in [runtime evidence](../analysis/AutoplayRuntimeEvidence.md)
  and [record-suppression evidence](../analysis/AutoplayRecordSuppressionEvidence.md).
  A human-authorized, non-mutating [Frida observation](../analysis/AutoplayRuntimeObservation-20261019.md)
  covered stock-list entry, pause/continue, replay through the list and standing
  up on the default build. It confirms that a retained chart is not gameplay
  authority; it does not qualify automatic notes, scores or another build.
- That observational run exited normally without changing the installed proxy.
  Subsequently, the human authorized installing the reviewed autoplay candidate
  while the game was closed. Prepared session
  `native-proxy-20261019T155834Z-622c330c08694682a501ed11565325c1`
  owned that candidate proxy; the product ASI remained disabled. The prior session was
  collected and rolled back before preparation, not overwritten. Both proxy
  binaries, reviewed receipts and the prior-version recovery plan are retained
  under `%LOCALAPPDATA%/FF7RPianoSongs/captures/autoplay-integration-20261019T155755Z-541c7300`.
  The human reported missing custom songs. The log shows completed song loading
  and repeated catalog-adoption rejection at `callback_gate_not_exclusive`.
  After human exit, the session was collected and rolled back; the exact previous
  proxy was restored through prepared session
  `native-proxy-20261019T163007Z-e5344e5c7ab447ecadd1eb90c09e401f`.
  The product ASI remains disabled. The failed candidate/log and prior bytes remain
  retained. Root cause: the new enclosing controller tick held the shared callback
  lease across native forwarding, so nested list adoption was never its sole
  owner. The correction isolates tick lifetime accounting and drains it before
  dependent service teardown, without weakening adoption's concurrency check.
  Independent concurrency review found no blockers. Corrected ASI/proxy builds
  pass for both catalog builds; the three correction tests plus catalog audit
  pass for the older build, and ten selected tests pass with the default restored.
  Final default DLL inspection again confirms no WinMM/waveOut imports.
  The human authorized a corrected-candidate installation after these checks;
  no automatic-note, audible-feedback or record-preservation success is claimed.
  No diagnostic product binary or release package was installed.
- The corrected autoplay candidate was accepted and retained in session
  `native-proxy-20261019T164143Z-1eeab49b65e746e791bba976376246e7`.
  The temporary previous-version restoration session above was rolled back
  without a gameplay run before this installation. Both artifacts, receipts,
  score baseline and recovery plan are retained in
  `%LOCALAPPDATA%/FF7RPianoSongs/captures/autoplay-adoption-fix-20261019T164115Z-57055649`.
  The human reported that autoplay works very well and the game was closed.
  Post-exit collection verifies successful custom catalog adoption with no
  `callback_gate_not_exclusive` recurrence. The log records custom Amadeus chart
  activation, three logical play generations, on/off transitions and an assisted
  final play reaching results then returning to the list. No error/fatal or
  feedback-failure marker appears in this log. The custom scores INI is byte-identical
  to the pre-run snapshot; this does not establish completed-custom-play or stock
  personal-best preservation. Per-song timing, audible signal distinction, exact
  input-device coverage and native record behavior were not separately confirmed.
  The session was collected and finalized `Keep` with its bounded manual report;
  the reviewed development proxy remains installed and rollback evidence retained.
  This is a successful focused development check, not release qualification or
  gameplay qualification for the older build.

## Second-play chart rejection investigation (2026-10-19)

- A reported same-song replay after a difficulty change rejected the final selected-row capture. The old `selected_provider_changed` marker also covered capture failure; its zero output does not prove provider drift or a catalog-republication defect. Earlier captures and audio admission succeeded.
- Added bounded first-failure capture facts and separated failed capture from observed identity drift, without changing validation, native reads, or fallback behavior. Independent scoped review found no actionable issues; the runtime, adapter, selected-row and extended-composition selftests pass.
- The diagnostic proxy session `native-proxy-20261019T130912Z-1ce0c5f8fea440aaa8502a33396912df` was collected and rolled back after a subsequent startup failure. The rollback restored its prior product ASI. This supersedes the installed-artifact disposition below, not its historical acceptance evidence.
- Original replay log/DLL, source snapshots, reviewed receipt and focused test output are retained under `%LOCALAPPDATA%/FF7RPianoSongs/captures/replay-chart-20261019T155547`. A new startup log is retained under the sibling `cache-setup-failure-20261019T164851` directory: GU object-count growth during a binding uniqueness scan produced `gu_header_drift`, terminal native bindings failure, and repository publication rejection.
- The startup recovery candidate treats only readable, unchanged-storage count growth with unique scanned candidates as `IncompleteSnapshot`, not success. Initial authentication remains pending for the next existing updater; acquisitions and registration are not repeated. Ambiguity, storage drift and post-bridge failures still fail closed. Independent review passed; bindings, runtime, adapter, selected-row and extended-composition selftests pass, including repeated deferral, recovery and an ambiguous appended tail.
- The reviewed recovery proxy was installed through session `native-proxy-20261019T135529Z-20c6d22425cf49119954c19e8ad602db`; its source/binary/receipt/test evidence is in the startup-failure capture directory above. That session was later collected and rolled back for the authorized autoplay installation above; its proxy was preserved for recovery. The original replay failure remains unresolved, and no Release qualification is claimed. Forced reproduction is not required.

## Offline MIDI generation investigation (2026-10-18)

- Corrected the historical reference corpus provenance: retained “VanillaPiano” assets are modded, not stock; historical route settings are not newly qualified stock/controller limits.
- Added the [stock export/comparison benchmark](VanillaBenchmark.md) and repaired the optional parent decoder, retaining required-input/group/control identities separately from musical attacks.
- Generated-policy v22 retains simultaneous tracked RH + representable LH pairs and adds the [source-exact LH/local-selection corrections](../analysis/MidiSelectionCorrections-20261018.md); policy-keyed generated caches rebuild, authored formats and binary layout are unchanged.
- The [eight-chart roundtrip](../analysis/MidiGenerationRoundtrip-20261018.md) improves attack retention but also increases some uninterrupted input series. It does not prove reduced fatigue or solve all LH underuse.
- The [installed-song review](../analysis/InstalledSongGenerationReview-20261018.md) exposed wrong-register full/superset LH admission. The follow-up now requires literal source pitches and truthful witnesses; existing exact reduced bass/two-tone carriers retain compatible accompaniment without invented octaves. Across the final isolated corpus, independently detected LH mismatch rows fall from 1,038 to zero. Unavailable or spacing-conflicting harmony can still be reduced or omitted.
- Generated-policy v23 ranks equally exact partial/reduced LH carriers by an emitted carrier root, fewest ignores, accompaniment pitch-class agreement, plain major/minor triads, then whole-onset agreement before lexical ID. The lexical rule had named lone C3 `pca_A_dim` and favored dim/sus4 names over the heard harmony; emitted sound is unchanged.
- Generated-policy v24 raises RH floors toward stock minima (0.35..0.15 s), lowers LH floors (3.0..0.45 s) so accompaniment and chord repeats fit, and limits RH root changes under 0.25 s to repeats or steps of at most 4 semitones. v25 widens that fast step to 7 (Lv5) and 12 (Lv6) semitones, because one fixed step made dense leaping sources such as Everlasting Summer select identical Lv4..Lv6 actions. Calibrated from stock statistics; game feel is unverified.
- Existing spacing floors now apply to native frames as well as source seconds, and selection uses a soft categorical-assignment switch preference. Independent review finds mixed local outcomes and longer series in some profiles; this is not proof of uniformly reduced fatigue.
- A bounded representative-root exchange improves source coverage without increasing required inputs or combined switch cost. All 30 isolated songs / 178 profiles prepare successfully; the Gate/Megalovania Lv6 gaps gain source-backed anchors, with no missing labels/endings and unchanged authored Rufus. All eight focused CTests pass. Remaining lower-level gaps and perceptual importance/controller comfort are not claimed solved.
- These are offline development changes, not changes to the installed artifact qualified below. Generated dual sound/scoring/tail checks, authenticated-audio listening and actual stick/retrigger/window semantics remain unqualified. No game launch, install or release package was performed for this work.

## Implemented Now

- Strict song discovery, JSON v2 explicit charts/profiles, MIDI generation,
  WAV/MP3/FLAC decoding, mode-specific audio processing, HCA/MABF generation and
  validated cache reuse. Authoring inputs are unchanged; see [Song Format](SongFormat.md).
- Automatic immutable native Music/BGMList/PianoScoreInfo/PianoScore containers
  built from authenticated source templates and generated audio. They contain
  genuine PackageStore metadata; users do not install generated pak files.
- Native mounting, independently retained DataObject acquisitions and additive
  row registration behind exact-build startup, identity, signature, ownership
  and exclusion guards. Artifact preparation is not runtime readiness.
- Genuine per-song selected names and retained chart providers, preserving
  existing list/profile/progress/score integration. Chart projection uses the
  exact selected PianoScore row and a reversible journal, not a first-basename
  stock-row match.
- The audio adapter no longer patches Sound/source fields, replays PlaySetup or
  orchestrates Set/Play/Stop. Native playback owns those transitions; the adapter
  retains chart/selection authority and readiness checks.
- Native decoding first uses the selected executable's authenticated decoder
  and default callbacks. An explicitly hash-qualified companion is a fallback,
  not a requirement when the embedded decoder is admitted.
- Offline-only builds remain separate from game memory, hook and runtime-tool
  dependencies. Native cache generation and source parsing run on repository
  workers, never in game callbacks.

## Per-Build Qualification

| Build | Current disposition | What is not established |
| --- | --- | --- |
| 1.005 | Development ASI accepted/kept after native audio, distinct-song chart and extended-chart scenarios; the later shared-code regression exercised the embedded executable decoder successfully. The owned `D3DCOMPILER_43.dll` also has a positive user report with native startup and two distinct song plans recorded. | Other proxy basenames, exhaustive songs/profiles/notes, separately witnessed pause/retry/stock transitions on the latest artifact, and complete lifecycle or Release qualification. |
| 1.004 | Separate bootstrap/runtime/asset profiles implemented; experimental ASI and full-product proxy archives built with affected offline/mapped tests passing. | Authentic older installed-asset validation, real startup, decoder interoperability and gameplay. Friend testing is pending. |

Exact executable identities, addresses and signatures remain in the
[RVA catalog](../src/game/rva_catalog.json). Older profiles were recovered from
the explicitly selected imported program; they are not authenticated older-disk
or gameplay evidence. Different virtual slots and inherited Music properties
are selected per build, not translated by RVA deltas.

## Current Evidence And Artifact Disposition

- `20261017T171411Z-4ef691a66203`: accepted 1.005 multi-song run. Amadeus,
  Everlasting Summer and Gate of Steiner used distinct projections; the latter
  committed 695 native events, including its extended tail. The user reported
  successful play and exited normally. The reviewed Development artifact was kept.
- `20261017T185024Z-b89afffc9fc4`: accepted newer-first shared-code regression.
  The log records `decoder_ready source=verified_executable`, all native cohorts
  Ready, separate Amadeus/Beneath The Mask preparations and an 845-event extended
  commit without rejection. The user reported successful play and normal exit.
  This tested ASI remains the archived rollback artifact; the older build did not
  replace it. It remains archived and disabled while the kept proxy below is installed.
- The experimental `FF7RPianoSongs-1.004-test.zip` contains only its selected-build
  ASI and mod INI with debug logging. Binary/provenance and ZIP contents were
  hash-verified. This is a friend-test artifact, not a qualified public release.

Immutable collection, manual receipts, tested binaries, provenance and rollback
copies remain in the corresponding runtime-gate sessions. Readiness collection
alone is not audible playback or note-correctness evidence. The bounded
[native integration record](../analysis/NativeAudioIntegration-20261017.md)
summarizes earlier failed runs and their corrections; those failures must not be
mistaken for the current artifact's disposition.

## Owned Proxy Development Distribution

A single-DLL proxy build contains the full mod as an alternative to the ASI
distribution. Its owned entry route does not depend on foreign-loader/CRT startup
profiles and does not load a sibling ASI. The default filename is
`D3DCOMPILER_43.dll`; optional profiles are `d3d9.dll`, `d3d11.dll`, `dsound.dll`,
`winmm.dll`, `dxgi.dll` and `xinput1_3.dll`. These names were taken from the
authenticated 1.005 executable's normal imports, not from a generic DLL-name list.
The selected filename must actually be a normal import of the target executable;
older import-name parity has not been independently established. Arbitrary
renaming or late `LoadLibrary` does not provide the required early origin.
The refreshed `FF7RPianoSongs-1.004-proxy-test.zip` contains one
`D3DCOMPILER_43.dll`, the mod INI with debug logging, and the user documentation.
Its selected-build artifact, generated entry profile, callback/export contracts
and archive contents were checked independently. It was not installed locally;
the kept 1.005 proxy is unchanged. This remains a test build pending older-game
startup and gameplay evidence.
Existing occupied proxy files must not be overwritten or chained implicitly.
The proxy and `FF7RPianoSongs.asi` are alternative distributions of the same mod;
do not load both together.

The entry transport restores the original executable entry before initializing
forwarding or the internal mod startup. Genuine System32 exports are resolved
separately. Executable authentication, the module-private active entry scope,
cold-state checks and later native-initializer/ownership gates still apply; a
caller-supplied receipt grants no native readiness. See the
[static design evidence](../analysis/NativeProxyBootstrap-20261018.json).

Both build-specific full proxy DLLs compile. Component forwarding/entry/private
origin tests, an owned synthetic static-import process, linked export inspection
and scoped native-safety reviews have passed. In current-build Debug experiment
`native-proxy-20261018T120416Z-a05267627b514b1d87eafab230391edc`, Windows loaded the
app-local `D3DCOMPILER_43.dll`, but no product log or readiness appeared. The exact
built callback was a leaf tail jump without the unwind record required by its
authenticator; the actual runtime rejection predicate was not captured. Normal
process exit was followed by hash-verified rollback to the accepted ASI. Existing
DXGI, XInput and mod INI files were unchanged. The immutable session/manual receipt
and checkpoints under LocalAppData captures preserve this failed test. The callback now performs
post-call bookkeeping; a disk-only COFF/PE regression verifies the emitted DLL's
real call/return, post-call store and complete unwind coverage. Rejection also
preserves copied first-failure facts in a new, non-overwriting
`FF7RPianoSongs.proxy-startup-failure.log` beside the DLL. Focused tests and review
passed. In follow-up Debug session
`native-proxy-20261018T123852Z-191f5a3f259247d5875b84b52d3a41fa`, the corrected
full-product `D3DCOMPILER_43.dll` recorded `source=owned_proxy_entry`, normal native
initialization, repository settlement and separate Amadeus/Believe Me chart
preparations. The user reported that it worked well. The assertion-backed
`successful-proxy-checkpoint.json` and exact log snapshot preserve that bounded
observation; other basenames, older gameplay and exhaustive lifecycle scenarios
were not tested. After confirmed process exit, immutable collection and a separate
reviewed manual receipt were saved, and the session was finalized with `keep`.
The tested proxy remains installed; the accepted ASI stays archived and disabled.
DXGI, XInput and mod settings were unchanged. No broader loader, OS or release
compatibility is claimed.

An owned-route portability audit found no active pin to this computer's UAL,
UCRT or ntdll build. Distribution still requires the chosen System32 backing
library, the dynamic MSVC runtime, writable song and LocalAppData caches, and
the supported stock archive/template layout. The export union is not an arbitrary
plugin-compatible system-DLL clone. Non-ASCII source paths and alternate proxy
names/plugin combinations remain unverified; the legacy ASI's OS pins must not
be confused with the reachable owned route.

## Remaining Limits

- Startup requires an actually witnessed early executable continuation and the
  later normal native initializer. The owned proxy validates its private restored
  entry scope; the alternative ASI route retains its qualified loader/Windows/CRT
  profiles. Unsupported sources reject; late pointers cannot reconstruct missed
  authority. No global ASI Loader INI change is required or performed.
- Native containers, acquired references and registered backing are retained
  until process exit. Release, unmount, live ASI unload/reload and same-process
  teardown are not qualified; active original-entry forwarding retains its hooks
  and module. Replace the product DLL or ASI only with the game stopped.
- Custom audio generation supports the recovered single-cue/three-mode Music
  profiles, not arbitrary Music serialization, multi-cue sources or author-supplied
  HCA/MABF/containers. Sources remain WAV/MP3/FLAC plus explicit notes or MIDI.
- Ordinary and extended chart bounds, grouping, input/chord capabilities and
  exclusions remain as documented in [Chart Limits](ChartLimits.md) and
  [Song Format](SongFormat.md). Older static parity does not establish older
  input, sound or chart behavior in-game.
- Matching-build HCA structural/metadata/decode evidence is bounded, not universal
  encoder parity. See [HCA Validation](HcaValidation.md).
- Historical release-specific manual-coverage decisions do not waive failures
  or qualify this development backend. See the
  [earlier development milestones](../analysis/DevelopmentMilestones-202609.md).

## Verification Boundary

The affected producer, decoder, startup, native runtime, adapter, chart/profile,
catalog and publication tests pass for the built targets. Installed-source
short/long generation also passed with immutable cache reuse and distinct
per-build output. Mapped native-call doubles do not replace real engine execution.
The historical development checks above are not qualification of a new package.
Version-scoped manual-coverage decisions are recorded separately above; a new archive
still requires its own clean identity and the canonical validation workflow.
