"""Source-boundary check; runtime owner/latch tests exercise the called machinery."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / "src/main.cpp").read_text(encoding="utf-8")
early = main.split("DWORD startup_worker_body()", 1)[1].split("DWORD WINAPI worker_thread", 1)[0]
late = main.split("DWORD WINAPI complete_startup", 1)[1].split("void start_completed_startup", 1)[0]
callback = main.split("void native_initialized()", 1)[1].split("DWORD startup_worker_body", 1)[0]

assert early.index("load_config(") < early.index("if (!config.enabled)") < early.index("install_native_protection_hooks(")
assert "g_config = config" in early
assert early.index("install_native_protection_hooks(") < early.index("native_barriers_published()")
late_only = ["initialize_uobject_identity(", "configure_autoplay_input(", "configure_song_preview(",
             "menu_theme_runtime::configure(", "configure_extended_chart_capability(",
             "configure_catalog_readiness_observer(", "register_muscriptor_transcriber(",
             "install_feature_hooks(", "install_startup_cache_overlay(", "start_title_input_service(",
             "start_thread_once(g_repository_started"]
positions = [late.index(operation) for operation in late_only]
assert positions == sorted(positions)
assert all(operation not in early and operation not in callback for operation in late_only)
assert "if (!game::install_feature_hooks(exe))" in late
failure = late.split("if (!game::install_feature_hooks(exe))", 1)[1].split("install_startup_cache_overlay(", 1)[0]
assert "return DWORD{0};" in failure
assert "start_completed_startup()" in callback and "complete_startup(" not in callback
queue = main.split("void start_completed_startup()", 1)[1].split("void native_initialized()", 1)[0]
assert "start_thread_once(g_completion_started, complete_startup" in queue
runtime = (root / "src/game/runtime.cpp").read_text(encoding="utf-8")
protection = runtime.split("bool install_native_protection_hooks(", 1)[1].split("bool install_feature_hooks(", 1)[0]
features = runtime.split("bool install_feature_hooks(", 1)[1]
assert protection.index("scope_present()") < protection.index("install_native_audio_runtime(")
assert protection.index("install_native_audio_runtime(") < protection.index("native_audio_ok\n        && install_piano_page_selection_guard(")
assert protection.index("install_piano_page_selection_guard(") < protection.index("g_native_protection_installed = early_ok") < protection.index("return early_ok;")
assert "install_piano_page_selection_guard(" not in features
early_failure = early.split("if (!game::install_native_protection_hooks(", 1)[1].split("native_barriers_published()", 1)[0]
assert "return 0;" in early_failure
guard = (root / "src/game/piano_page_selection_guard.cpp").read_text(encoding="utf-8")
install = guard.split("InstallResult install(HMODULE exe_module)", 1)[1].split("#else", 1)[0]
assert install.index("scope_present()") < install.index("attempted = true") < install.index("replace_rel32_call_transactionally(")
assert "attempted || core::hook_trampolines_retained_for_process_lifetime()" in install
native = (root / "src/game/native_audio_runtime.cpp").read_text(encoding="utf-8")
transaction = native.split("core::OwnedHookBatch batch;", 1)[1].split("void native_audio_updater_observed", 1)[0]
# All origins use the same batch; ASI/post-CRT omit the already-held/passed AEP.
omit = "if((synchronous || proxy_post_crt) && h.detour==reinterpret_cast<void*>(&ordinary_entry_hook))continue;"
assert transaction.index(omit) < transaction.index("batch.prepare(") < transaction.index("batch.apply(")
assert transaction.count("batch.apply(") == 1
assert transaction.index("batch.apply(") < transaction.index('"publication.finish"') < transaction.index("g_accepting.store(true")
assert "Release PME/MinHook serialization before native callback/lifetime teardown" in transaction
assert transaction.index("batch.cancel(") < transaction.index("shutdown_native_audio_runtime()")
assert transaction.index("g_accepting.store(true") < transaction.index("if(synchronous || proxy_post_crt)") < transaction.index("g_origin.store(true")
assert "active_entry_scope() || ff7rp::proxy::active_call_scope()" in native
proxy = (root / "src/proxy/proxy_main.cpp").read_text(encoding="utf-8")
entry = proxy.split("bool initialize_at_entry(", 1)[1].split("bool attach_process_start(", 1)[0]
post = entry.split("if constexpr(proxy::generated::kOwnedCallEnabled)", 1)[1].split("if (!proxy::initialize_owned_product", 1)[0]
assert entry.index("initialize_forwarding(self_module)") < entry.index("arm_owned_call(")
assert "initialize_owned_product" not in post
assert "return armed && status.stage==bridge::CallStage::Armed;" in post
callback = proxy.split("bool initialize_at_call(", 1)[1].split("void entry_completed()", 1)[0]
assert callback.index("scope.capture(proof)") < callback.index("initialize_owned_product(")
assert "persist_startup_failure" not in callback and "emit_native_install_diagnostic" not in callback
assert "persist_startup_failure" in proxy.split("void call_completed()", 1)[1].split("bool initialize_at_call", 1)[0]
bootstrap = (root / "src/game/native_proxy_bootstrap.cpp").read_text(encoding="utf-8")
post_validation = bootstrap.split("if(s.post)", 1)[1].split("for(unsigned i=0;i<n;++i)", 2)[0]
assert "active_call_scope()" in post_validation and "held!=s.call_bridge" in post_validation
assert "current.return_pc!=reinterpret_cast<Address>(s.exe)+call->rva+5" in post_validation
assert '"post_crt_return_unwind"' in bootstrap and '"post_current_chain"' in bootstrap
print("startup_phase_order_selftest: passed (source boundaries only)")
