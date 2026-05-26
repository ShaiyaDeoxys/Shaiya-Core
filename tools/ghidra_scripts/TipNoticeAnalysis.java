// Decompile functions that reference tip.txt and notice.txt
// @category Analysis

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.util.*;

public class TipNoticeAnalysis extends GhidraScript {

    @Override
    public void run() throws Exception {
        Listing listing = currentProgram.getListing();
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        // tip.txt push at 0x431A40 (push 0x74A3A4), function should contain this
        // notice.txt push at 0x4E19E0 (push 0x75060C)
        Address[] refs = new Address[] {
            space.getAddress(0x431A40),
            space.getAddress(0x4E19E0)
        };
        String[] names = new String[] { "tip.txt", "notice.txt" };

        for (int idx = 0; idx < refs.length; idx++) {
            Address refAddr = refs[idx];
            Function func = listing.getFunctionContaining(refAddr);

            println("\n========================================");
            println("=== " + names[idx] + " reference at " + refAddr + " ===");

            if (func == null) {
                println("No function found containing " + refAddr);
                // Try to show disassembly around it
                println("Disassembly:");
                Address cur = refAddr.add(-32);
                for (int i = 0; i < 40; i++) {
                    Instruction insn = listing.getInstructionAt(cur);
                    if (insn != null) {
                        println("  " + cur + ": " + insn.toString());
                        cur = cur.add(insn.getLength());
                    } else {
                        cur = cur.add(1);
                    }
                }
                continue;
            }

            println("Function: " + func.getName() + " at " + func.getEntryPoint());
            println("Size: " + func.getBody().getNumAddresses() + " bytes");

            // Decompile
            DecompileResults results = decomp.decompileFunction(func, 30, monitor);
            if (results.decompileCompleted()) {
                String decompiledCode = results.getDecompiledFunction().getC();
                println("\n--- Decompiled C ---");
                println(decompiledCode);
            } else {
                println("Decompilation failed: " + results.getErrorMessage());
            }

            // Also show disassembly around the reference
            println("\n--- Disassembly around reference ---");
            Address cur = refAddr.add(-20);
            for (int i = 0; i < 30; i++) {
                Instruction insn = listing.getInstructionAt(cur);
                if (insn != null) {
                    String marker = (cur.getOffset() >= refAddr.getOffset() && cur.getOffset() < refAddr.getOffset() + 8) ? " <<<" : "";
                    println("  " + cur + ": " + insn.toString() + marker);
                    cur = cur.add(insn.getLength());
                } else {
                    cur = cur.add(1);
                }
            }
        }

        decomp.dispose();
        println("\n=== Done ===");
    }
}
