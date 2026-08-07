// Read-only Ghidra post-script for the shipping PianoScore event ABI.
// @category FF7R

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.Variable;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.mem.MemoryBlock;

public class RecoverChartEventAbi extends GhidraScript {
    private static final long[] TARGET_RVAS = {
        0x39b3608L, // parser
        0x3987734L, // event constructor
        0x3987624L, // event deep copy
        0x398db4cL, // link helper
        0x3988a04L, // temporary destructor
    };

    private static final long[] ACCESSOR_TABLE_RVAS = {
        0x6797670L, 0x6798670L, 0x6799670L, 0x679a670L,
        0x679d670L, 0x679e670L,
        0x679f670L, 0x67a0670L, 0x67a1670L, 0x67a2670L,
    };

    private Address address(long rva) {
        return currentProgram.getImageBase().add(rva);
    }

    private static final class RuntimeFunction {
        long begin;
        long end;
        long unwind;
    }

    private List<RuntimeFunction> readRuntimeFunctions() throws Exception {
        MemoryBlock pdata = currentProgram.getMemory().getBlock(".pdata");
        if (pdata == null) throw new IllegalStateException("missing .pdata block");
        List<RuntimeFunction> functions = new ArrayList<>();
        for (Address cursor = pdata.getStart(); cursor.add(11).compareTo(pdata.getEnd()) <= 0; cursor = cursor.add(12)) {
            RuntimeFunction function = new RuntimeFunction();
            function.begin = Integer.toUnsignedLong(currentProgram.getMemory().getInt(cursor));
            function.end = Integer.toUnsignedLong(currentProgram.getMemory().getInt(cursor.add(4)));
            function.unwind = Integer.toUnsignedLong(currentProgram.getMemory().getInt(cursor.add(8)));
            if (function.begin != 0 && function.end > function.begin) functions.add(function);
        }
        functions.sort((left, right) -> Long.compareUnsigned(left.begin, right.begin));
        return functions;
    }

    private RuntimeFunction containing(List<RuntimeFunction> functions, long rva) {
        int low = 0;
        int high = functions.size() - 1;
        while (low <= high) {
            int middle = (low + high) >>> 1;
            RuntimeFunction function = functions.get(middle);
            if (Long.compareUnsigned(rva, function.begin) < 0) high = middle - 1;
            else if (Long.compareUnsigned(rva, function.end) >= 0) low = middle + 1;
            else return function;
        }
        return null;
    }

    private Function materialize(long entryRva, RuntimeFunction runtimeFunction) throws Exception {
        long beginRva = runtimeFunction == null ? entryRva : runtimeFunction.begin;
        long endRva = runtimeFunction == null ? entryRva + 0x400 : runtimeFunction.end;
        Address begin = address(beginRva);
        Address end = address(endRva - 1);
        Function function = getFunctionAt(begin);
        Address bodyEnd = end;
        Address cursor = begin;
        while (cursor.compareTo(end) <= 0) {
            Instruction instruction = currentProgram.getListing().getInstructionAt(cursor);
            if (instruction == null) {
                if (!disassemble(cursor)) {
                    cursor = cursor.add(1);
                    continue;
                }
                instruction = currentProgram.getListing().getInstructionAt(cursor);
            }
            if (instruction == null) {
                cursor = cursor.add(1);
                continue;
            }
            cursor = instruction.getMaxAddress().add(1);
            if (runtimeFunction == null && "RET".equals(instruction.getMnemonicString())) {
                bodyEnd = instruction.getMaxAddress();
                break;
            }
        }
        if (function == null) function = createFunction(begin, "FUN_" + begin.toString());
        if (function != null) function.setBody(new AddressSet(begin, bodyEnd));
        return function != null ? function : getFunctionContaining(begin);
    }

    private Set<Long> directCallerRvas(Set<Long> targetRvas) throws Exception {
        Set<Long> callers = new LinkedHashSet<>();
        MemoryBlock text = currentProgram.getMemory().getBlock(".text");
        if (text == null) throw new IllegalStateException("missing .text block");
        Address cursor = text.getStart();
        Address last = text.getEnd().subtract(4);
        while (cursor.compareTo(last) <= 0) {
            if ((currentProgram.getMemory().getByte(cursor) & 0xff) == 0xe8) {
                int displacement = currentProgram.getMemory().getInt(cursor.add(1));
                Address destination = cursor.add(5L + displacement);
                long destinationRva = destination.subtract(currentProgram.getImageBase());
                if (targetRvas.contains(destinationRva)) {
                    callers.add(cursor.subtract(currentProgram.getImageBase()));
                }
            }
            cursor = cursor.add(1);
        }
        return callers;
    }

    private String functionName(Function function) {
        return function == null ? "<no-function>" : function.getName() + "@" + function.getEntryPoint();
    }

    private int unsignedByte(Address source) throws Exception {
        return currentProgram.getMemory().getByte(source) & 0xff;
    }

    private long unsignedInt(Address source) throws Exception {
        return Integer.toUnsignedLong(currentProgram.getMemory().getInt(source));
    }

    private long readCompressedUnsigned(Address[] cursor) throws Exception {
        int first = unsignedByte(cursor[0]);
        int[] lengths = {1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 5};
        int length = lengths[first & 0xf];
        long value;
        if (length == 5) {
            value = 0;
            for (int i = 1; i < 5; ++i) {
                value |= (long)unsignedByte(cursor[0].add(i)) << ((i - 1) * 8);
            }
        } else {
            value = first >>> length;
            for (int i = 1; i < length; ++i) {
                value |= (long)unsignedByte(cursor[0].add(i)) << (i * 8 - length);
            }
        }
        cursor[0] = cursor[0].add(length);
        return value & 0xffffffffL;
    }

    private void dumpAccessorTables(PrintWriter out) throws Exception {
        out.println("GENERATED_ACCESSOR_TABLE_BOUNDARY");
        for (long tableRva : ACCESSOR_TABLE_RVAS) {
            out.println("  table=0x" + Long.toHexString(tableRva));
            for (int row = 508; row <= 513; ++row) {
                Address slot = address(tableRva).add(row * 8L);
                long pointer = currentProgram.getMemory().getLong(slot);
                Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(pointer);
                Function function = currentProgram.getMemory().contains(target) ? getFunctionContaining(target) : null;
                out.println("    row=" + row + " slot=" + slot + " target=" + target
                    + " function=" + functionName(function));
            }
        }
        out.println();
    }

    private Set<Function> dumpExceptionMetadata(
            PrintWriter out, List<RuntimeFunction> runtimeFunctions, RuntimeFunction runtimeFunction) throws Exception {
        Set<Function> cleanupFunctions = new LinkedHashSet<>();
        out.println("PARSER_EXCEPTION_METADATA");
        if (runtimeFunction == null) {
            out.println("  <no runtime function>");
            out.println();
            return cleanupFunctions;
        }

        Address unwind = address(runtimeFunction.unwind);
        int versionAndFlags = unsignedByte(unwind);
        int flags = versionAndFlags >>> 3;
        int codeCount = unsignedByte(unwind.add(2));
        long handlerOffset = 4L + ((codeCount * 2L + 3L) & ~3L);
        out.println("  runtime_begin=0x" + Long.toHexString(runtimeFunction.begin)
            + " runtime_end=0x" + Long.toHexString(runtimeFunction.end)
            + " unwind=0x" + Long.toHexString(runtimeFunction.unwind)
            + " version=" + (versionAndFlags & 7) + " flags=0x" + Integer.toHexString(flags)
            + " prolog_size=" + unsignedByte(unwind.add(1)) + " code_count=" + codeCount);
        if ((flags & 3) == 0) {
            out.println("  <no exception handler>");
            out.println();
            return cleanupFunctions;
        }

        long handlerRva = unsignedInt(unwind.add(handlerOffset));
        long handlerData = unsignedInt(unwind.add(handlerOffset + 4));
        Function handler = getFunctionContaining(address(handlerRva));
        out.println("  handler=0x" + Long.toHexString(handlerRva) + " function=" + functionName(handler)
            + " handler_data=0x" + Long.toHexString(handlerData));
        if (handlerRva == 0x216bae8L) {
            out.println("  handler_data_kind=gs_cookie_frame_offset frame_offset=0x"
                + Long.toHexString(handlerData));
            out.println("  cxx_unwind_map=absent");
            out.println();
            return cleanupFunctions;
        }

        Address[] cursor = { address(handlerData) };
        int header = unsignedByte(cursor[0]);
        cursor[0] = cursor[0].add(1);
        if ((header & 0x04) != 0) readCompressedUnsigned(cursor);
        long unwindMapRva = (header & 0x08) != 0 ? unsignedInt(cursor[0]) : 0;
        if ((header & 0x08) != 0) cursor[0] = cursor[0].add(4);
        long tryBlockMapRva = (header & 0x10) != 0 ? unsignedInt(cursor[0]) : 0;
        if ((header & 0x10) != 0) cursor[0] = cursor[0].add(4);
        long ipToStateMapRva = unsignedInt(cursor[0]);
        cursor[0] = cursor[0].add(4);
        long frameOffset = (header & 0x01) != 0 ? readCompressedUnsigned(cursor) : 0;
        out.println("  func_info_header=0x" + Integer.toHexString(header)
            + " is_catch=" + ((header & 0x01) != 0)
            + " is_separated=" + ((header & 0x02) != 0)
            + " unwind_map=0x" + Long.toHexString(unwindMapRva)
            + " try_block_map=0x" + Long.toHexString(tryBlockMapRva)
            + " ip_to_state_map=0x" + Long.toHexString(ipToStateMapRva)
            + " frame_offset=0x" + Long.toHexString(frameOffset));

        if (unwindMapRva != 0) {
            cursor[0] = address(unwindMapRva);
            long count = readCompressedUnsigned(cursor);
            out.println("  UNWIND_MAP entries=" + count);
            for (int state = 0; state < count; ++state) {
                long encoded = readCompressedUnsigned(cursor);
                int type = (int)(encoded & 3);
                long nextOffset = encoded >>> 2;
                long actionRva = 0;
                long objectOffset = 0;
                if (type == 1 || type == 2) {
                    actionRva = unsignedInt(cursor[0]);
                    cursor[0] = cursor[0].add(4);
                    objectOffset = readCompressedUnsigned(cursor);
                } else if (type == 3) {
                    actionRva = unsignedInt(cursor[0]);
                    cursor[0] = cursor[0].add(4);
                }
                Function action = null;
                if (actionRva != 0) {
                    RuntimeFunction actionRuntime = containing(runtimeFunctions, actionRva);
                    action = materialize(actionRva, actionRuntime);
                    if (action != null) cleanupFunctions.add(action);
                }
                out.println("    state=" + state + " next_offset=" + nextOffset + " type=" + type
                    + " action=0x" + Long.toHexString(actionRva)
                    + " object_offset=0x" + Long.toHexString(objectOffset)
                    + " function=" + functionName(action));
            }
        }

        if (ipToStateMapRva != 0 && (header & 0x02) == 0) {
            cursor[0] = address(ipToStateMapRva);
            long count = readCompressedUnsigned(cursor);
            long previousIp = 0;
            out.println("  IP_TO_STATE_MAP entries=" + count);
            for (int entry = 0; entry < count; ++entry) {
                previousIp += readCompressedUnsigned(cursor);
                long state = readCompressedUnsigned(cursor) - 1;
                out.println("    entry=" + entry + " ip=0x"
                    + Long.toHexString(runtimeFunction.begin + previousIp) + " state=" + state);
            }
        }
        out.println();
        return cleanupFunctions;
    }

    private void dumpFunction(PrintWriter out, DecompInterface decompiler, Function function) {
        out.println("================================================================================");
        out.println("FUNCTION " + functionName(function));
        out.println("BODY " + function.getBody());
        out.println("CALLING_CONVENTION " + function.getCallingConventionName());
        out.println("RETURN " + function.getReturnType());
        out.println("PARAMETERS");
        for (Parameter parameter : function.getParameters()) {
            out.println("  ordinal=" + parameter.getOrdinal() + " storage=" + parameter.getVariableStorage()
                + " type=" + parameter.getDataType() + " name=" + parameter.getName());
        }
        out.println("STACK_FRAME local_size=" + function.getStackFrame().getLocalSize()
            + " parameter_offset=" + function.getStackFrame().getParameterOffset());
        for (Variable variable : function.getStackFrame().getStackVariables()) {
            out.println("  offset=" + variable.getStackOffset() + " size=" + variable.getLength()
                + " type=" + variable.getDataType() + " name=" + variable.getName());
        }

        out.println("REFERENCES_TO_ENTRY");
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(function.getEntryPoint());
        while (references.hasNext()) {
            Reference reference = references.next();
            Function caller = getFunctionContaining(reference.getFromAddress());
            out.println("  from=" + reference.getFromAddress() + " type=" + reference.getReferenceType()
                + " caller=" + functionName(caller));
        }

        out.println("DECOMPILE");
        DecompileResults result = decompiler.decompileFunction(function, 300, monitor);
        if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
            out.println(result.getDecompiledFunction().getC());
        } else {
            out.println("<failed> " + result.getErrorMessage());
        }

        out.println("DISASSEMBLY");
        InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
        while (instructions.hasNext()) {
            Instruction instruction = instructions.next();
            StringBuilder line = new StringBuilder();
            line.append(instruction.getAddress()).append("  ").append(instruction);
            Reference[] flows = instruction.getReferencesFrom();
            if (flows.length != 0) {
                line.append("  ; refs=");
                for (int i = 0; i < flows.length; ++i) {
                    if (i != 0) line.append(",");
                    line.append(flows[i].getReferenceType()).append(":").append(flows[i].getToAddress());
                }
            }
            out.println(line);
        }
        out.println();
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) {
            throw new IllegalArgumentException("expected output path");
        }

        File output = new File(args[0]);
        File parent = output.getParentFile();
        if (parent != null) parent.mkdirs();

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("failed to open program in decompiler");
        }

        try (PrintWriter out = new PrintWriter(output, "UTF-8")) {
            out.println("PROGRAM " + currentProgram.getName());
            out.println("IMAGE_BASE " + currentProgram.getImageBase());
            out.println("LANGUAGE " + currentProgram.getLanguageID());
            out.println();

            List<RuntimeFunction> runtimeFunctions = readRuntimeFunctions();
            Set<Long> targetRvas = new LinkedHashSet<>();
            for (long rva : TARGET_RVAS) targetRvas.add(rva);
            Set<Long> callerRvas = directCallerRvas(targetRvas);

            Set<Function> functions = new LinkedHashSet<>();
            for (long rva : TARGET_RVAS) {
                Address target = address(rva);
                RuntimeFunction runtimeFunction = containing(runtimeFunctions, rva);
                Function function = materialize(rva, runtimeFunction);
                if (function == null) {
                    out.println("MISSING RVA 0x" + Long.toHexString(rva) + " address=" + target);
                } else {
                    out.println("RUNTIME_FUNCTION target=0x" + Long.toHexString(rva)
                        + " begin=0x" + (runtimeFunction == null ? "leaf" : Long.toHexString(runtimeFunction.begin))
                        + " end=0x" + (runtimeFunction == null ? "leaf" : Long.toHexString(runtimeFunction.end))
                        + " unwind=0x" + (runtimeFunction == null ? "none" : Long.toHexString(runtimeFunction.unwind)));
                    functions.add(function);
                }
            }

            dumpAccessorTables(out);
            functions.addAll(dumpExceptionMetadata(
                out, runtimeFunctions, containing(runtimeFunctions, TARGET_RVAS[0])));

            out.println("DIRECT_CALL_SITES");
            for (long callerRva : callerRvas) {
                RuntimeFunction runtimeFunction = containing(runtimeFunctions, callerRva);
                Function caller = materialize(callerRva, runtimeFunction);
                out.println("  call=0x" + Long.toHexString(callerRva)
                    + " caller_begin=0x" + (runtimeFunction == null ? "?" : Long.toHexString(runtimeFunction.begin))
                    + " caller_end=0x" + (runtimeFunction == null ? "?" : Long.toHexString(runtimeFunction.end))
                    + " function=" + functionName(caller));
                if (caller != null) functions.add(caller);
            }
            out.println();

            Function parser = getFunctionAt(address(TARGET_RVAS[0]));
            out.println("PARSER_DIRECT_CALLEES");
            if (parser != null) {
                InstructionIterator parserInstructions = currentProgram.getListing().getInstructions(parser.getBody(), true);
                while (parserInstructions.hasNext()) {
                    Instruction instruction = parserInstructions.next();
                    if (!"CALL".equals(instruction.getMnemonicString())) continue;
                    for (Address destination : instruction.getFlows()) {
                        if (!currentProgram.getMemory().contains(destination)) continue;
                        long calleeRva = destination.subtract(currentProgram.getImageBase());
                        RuntimeFunction runtimeFunction = containing(runtimeFunctions, calleeRva);
                        Function callee = materialize(calleeRva, runtimeFunction);
                        out.println("  call=0x" + Long.toHexString(
                                instruction.getAddress().subtract(currentProgram.getImageBase()))
                            + " target=0x" + Long.toHexString(calleeRva)
                            + " function=" + functionName(callee));
                        if (callee != null) functions.add(callee);
                    }
                }
            }
            out.println();

            for (Function function : functions) {
                monitor.checkCancelled();
                dumpFunction(out, decompiler, function);
                out.flush();
            }
        } finally {
            decompiler.dispose();
        }
        println("Wrote " + output.getAbsolutePath());
    }
}
