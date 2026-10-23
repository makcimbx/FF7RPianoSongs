// Observation only; load through the lead's already-authorized Frida session.
// Default profile only. Lead must independently authenticate the disk SHA256.
// No hooks, NativeFunction, writes, timers, name construction, or native calls.
'use strict';

function previewEnvelope(d) {
    const start = d.getFloat32(8, true), target = d.getFloat32(12, true);
    const elapsed = d.getFloat32(16, true), duration = d.getFloat32(20, true);
    const curve = d.getInt32(24, true), f = Math.fround;
    const out = { start, target, elapsed, duration, curve, currentApprox: null };
    if (![start, target, elapsed, duration].every(Number.isFinite)) return out;
    if (duration === 0) { out.currentApprox = target; return out; }
    if (elapsed < 0) { out.currentApprox = start; return out; }
    const t = f(elapsed / duration), square = f(t * t);
    let weight;
    switch (curve) {
    case 0: weight = t; break;
    case 1: { const a = f(1 - square); weight = f(1 - f(f(a * a) * a)); break; }
    case 2: { const a = f(1 - t); weight = f(1 - f(a * a)); break; }
    case 3: weight = square; break;
    case 4: weight = f(f(Math.pow(2, t)) - 1); break;
    case 5: weight = f(2 - f(Math.pow(2, f(1 - t)))); break;
    default: return out; // Raw fields retained; no invented curve interpretation.
    }
    const value = f(start + f(f(target - start) * weight));
    if (Number.isFinite(value)) out.currentApprox = value;
    return out; // Diagnostic approximation, not native powf/atomic-snapshot parity.
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { previewEnvelope }; // Allows offline arithmetic tests only.
} else {
    const LIMIT = { categories: 256, layers: 32, nameBytes: 96, depth: 16, snapshots: 8 };
    let calls = 0;
    function read(p, n) {
        if (p.isNull() || n <= 0 || n > 4096) throw new Error('invalid_read_bound');
        // readVolatile checks accessibility itself; repeated full-map queries are too costly here.
        if (typeof p.readVolatile !== 'function') throw new Error('readVolatile_required');
        const bytes = p.readVolatile(n);
        if (bytes === null || bytes.byteLength !== n) throw new Error('short_read');
        return new DataView(bytes);
    }
    function pointer(d, offset) {
        return ptr('0x' + d.getUint32(offset + 4, true).toString(16)
            + d.getUint32(offset, true).toString(16).padStart(8, '0'));
    }
    function sameBytes(a, b) {
        return a.byteLength === b.byteLength && Array.from(new Uint8Array(a.buffer))
            .every((v, i) => v === b.getUint8(i));
    }
    function name(p) {
        if (p.isNull()) return { text: null, status: 'null' };
        try {
            const d = read(p, LIMIT.nameBytes); let text = '';
            for (let i = 0; i < d.byteLength; ++i) {
                const c = d.getUint8(i);
                if (c === 0) return { text, status: 'ascii_nul' };
                if (c < 32 || c > 126) return { text: null, status: 'non_ascii' };
                text += String.fromCharCode(c);
            }
            return { text: null, status: 'unterminated_at_cap' };
        } catch (e) { return { text: null, status: String(e) }; }
    }
    function identity(d) {
        return [d.getInt32(0x60, true), d.getInt32(0x64, true),
            pointer(d, 0x38).toString(), pointer(d, 0x78).toString(),
            d.getInt32(0x80, true), pointer(d, 0x90).toString()].join('|');
    }
    function snapshot() {
        if (++calls > LIMIT.snapshots) return { status: 'snapshot_cap', calls };
        try {
            if (Process.arch !== 'x64' || Process.platform !== 'windows' || Process.pointerSize !== 8)
                throw new Error('unsupported_platform');
            const m = Process.getModuleByName('ff7rebirth_.exe'), base = m.base;
            const dos = read(base, 0x40);
            if (dos.getUint16(0, true) !== 0x5a4d || dos.getUint32(0x3c, true) !== 0x210)
                throw new Error('dos_identity');
            const pe = read(base.add(0x210), 0x70);
            if (pe.getUint32(0, true) !== 0x4550 || pe.getUint16(4, true) !== 0x8664
                || pe.getUint16(0x18, true) !== 0x20b || pe.getUint32(8, true) !== 0x6a16ced2
                || pe.getUint32(0x50, true) !== 0x099d9000 || pe.getUint32(0x58, true) !== 0x0769ea6e)
                throw new Error('pe_identity');
            const stateAt = base.add(0x091d8320), state = read(stateAt, 0x70);
            const count = state.getInt32(0, true), array = pointer(state, 8);
            const initialized = state.getUint8(0x68), shutdown = state.getInt32(0x58, true);
            if (initialized !== 1 || shutdown !== 0 || array.isNull() || count === 0)
                return { status: 'not_ready', initialized, shutdown, count, array: array.toString() };
            if (count < 0 || count > LIMIT.categories) throw new Error('category_count_cap');
            const slots = read(array, count * 8), records = [], identities = new Map();
            for (let i = 0; i < count; ++i) {
                const p = pointer(slots, i * 8);
                if (p.isNull()) continue;
                if (identities.has(p.toString())) throw new Error('duplicate_category_pointer');
                const d = read(p, 0x98), layers = d.getInt32(0x80, true), envelopes = pointer(d, 0x38);
                if (layers < 0 || layers > LIMIT.layers) throw new Error('layer_count_cap');
                const r = { slot: i, address: p.toString(), id: d.getInt32(0x60, true),
                    parentId: d.getInt32(0x64, true), parent: pointer(d, 0x90).toString(),
                    namePointer: pointer(d, 0x78).toString(), name: name(pointer(d, 0x78)),
                    baseGain: d.getFloat32(0x48, true), suspendCount: d.getInt32(0x6c, true),
                    envelopes: envelopes.toString(), layerCount: layers, config: null };
                // Config enum=2; native getter indexes (layer + 1), not layer.
                if (layers > 3 && !envelopes.isNull()) r.config = previewEnvelope(read(envelopes.add(3 * 0x30), 0x30));
                if (identity(d) !== identity(read(p, 0x98))) throw new Error('category_identity_drift');
                identities.set(r.address, identity(d)); records.push(r);
            }
            if (!sameBytes(slots, read(array, count * 8))) throw new Error('category_slots_drift');
            for (const r of records)
                if (identities.get(r.address) !== identity(read(ptr(r.address), 0x98)))
                    throw new Error('category_identity_drift');
            const after = read(stateAt, 0x70);
            if (after.getInt32(0, true) !== count || !pointer(after, 8).equals(array)
                || after.getUint8(0x68) !== initialized || after.getInt32(0x58, true) !== shutdown)
                throw new Error('engine_state_drift');
            const bgm = records.filter(r => r.name.text !== null && r.name.text.toLowerCase() === 'bgm');
            const piano = records.filter(r => r.id === 31), chain = [], visited = new Set();
            let current = piano.length === 1 ? piano[0] : null, terminal = 'category31_missing_or_duplicate';
            while (current) {
                if (visited.has(current.address)) { terminal = 'parent_loop'; break; }
                if (chain.length >= LIMIT.depth) { terminal = 'parent_depth_cap'; break; }
                visited.add(current.address); chain.push(current.address);
                if (ptr(current.parent).isNull()) { terminal = 'null_parent'; break; }
                const parent = records.find(r => r.address === current.parent);
                if (!parent || parent.id !== current.parentId) { terminal = 'parent_missing_or_id_mismatch'; break; }
                current = parent;
            }
            return { status: 'observed_nonatomic', calls, profile: 'ff7rebirth-steam-win64-6a16ced2',
                base: base.toString(), initialized, shutdown, count, array: array.toString(), records,
                category31Chain: chain, terminal, bgm: bgm.map(r => r.address),
                reachesBgm: bgm.length === 1 && piano.length === 1 && terminal === 'null_parent'
                    ? chain.includes(bgm[0].address) : null };
        } catch (e) { return { status: 'unavailable', calls, reason: String(e) }; }
    }
    rpc.exports = { snapshot };
    send(snapshot()); // One immediate sample; at most seven further explicit RPC snapshots.
}
