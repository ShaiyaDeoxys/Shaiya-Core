// Decompile functions loading tip.txt and notice.txt
// @category Analysis

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;

public class TipNoticeAnalysis2 extends GhidraScript {

    @Override
    public void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        Listing listing = currentProgram.getListing();
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        // tip.txt string at VA 0x74A3A4
        // The push instruction is at 0x431A3F: 68 A4 A3 74 00
        Address tipPush = space.getAddress(0x431A3F);
        analyzeRef(listing, decomp, tipPush, "tip.txt");

        // notice.txt string at VA 0x75060C
        // Search for push 0x75060C (bytes: 68 0C 06 75 00)
        println("\n=== Searching for push 0x75060C (notice.txt) ===");
        byte[] pushNotice = new byte[] { 0x68, 0x0C, 0x06, 0x75, 0x00 };
        Address found = memory.findBytes(space.getAddress(0x400000), pushNotice, null, true, monitor);
        while (found != null) {
            println("Found push notice.txt at " + found);
            analyzeRef(listing, decomp, found, "notice.txt");
            found = memory.findBytes(found.add(1), pushNotice, null, true, monitor);
        }

        // Also check: maybe notice.txt is loaded via a different pattern (lea + push, or mov)
        // Search for mov reg, 0x75060C
        byte[] movNotice = new byte[] { (byte)0xB8, 0x0C, 0x06, 0x75, 0x00 }; // mov eax
        found = memory.findBytes(space.getAddress(0x400000), movNotice, null, true, monitor);
        if (found != null) {
            println("Found mov eax, notice.txt at " + found);
            analyzeRef(listing, decomp, found, "notice.txt (mov eax)");
        }

        // Also decompile the function called with tip.txt: 0x4D00F0
        println("\n========================================");
        println("=== Decompiling tip.txt loader function at 0x4D00F0 ===");
        Function loaderFunc = listing.getFunctionAt(space.getAddress(0x4D00F0));
        if (loaderFunc != null) {
            DecompileResults res = decomp.decompileFunction(loaderFunc, 30, monitor);
            if (res.decompileCompleted() && res.getDecompiledFunction() != null) {
                println(res.getDecompiledFunction().getC());
            } else {
                println("Decompilation failed");
            }
        } else {
            println("No function at 0x4D00F0, showing disassembly");
            Address cur = space.getAddress(0x4D00F0);
            for (int i = 0; i < 40; i++) {
                Instruction insn = listing.getInstructionAt(cur);
                if (insn != null) {
                    println("  " + cur + ": " + insn);
                    cur = cur.add(insn.getLength());
                } else { cur = cur.add(1); }
            }
        }

        // Decompile function containing the tip.txt push
        println("\n========================================");
        println("=== Decompiling function containing tip.txt push ===");
        Function tipFunc = listing.getFunctionContaining(tipPush);
        if (tipFunc != null) {
            println("Function: " + tipFunc.getName() + " at " + tipFunc.getEntryPoint());
            // Show full disassembly
            Address cur = tipFunc.getEntryPoint();
            Address end = tipFunc.getBody().getMaxAddress();
            int count = 0;
            while (cur != null && cur.compareTo(end) <= 0 && count < 80) {
                Instruction insn = listing.getInstructionAt(cur);
                if (insn != null) {
                    println("  " + cur + ": " + insn);
                    cur = cur.add(insn.getLength());
                } else { cur = cur.add(1); }
                count++;
            }

            DecompileResults res = decomp.decompileFunction(tipFunc, 30, monitor);
            if (res.decompileCompleted() && res.getDecompiledFunction() != null) {
                println("\n--- Decompiled C ---");
                println(res.getDecompiledFunction().getC());
            }
        }

        decomp.dispose();
        println("\n=== Done ===");
    }

    void analyzeRef(Listing listing, DecompInterface decomp, Address addr, String name) throws Exception {
        println("\n========================================");
        println("=== " + name + " at " + addr + " ===");
        Function func = listing.getFunctionContaining(addr);
        if (func == null) {
            println("No function containing " + addr);
            return;
        }
        println("In function: " + func.getName() + " at " + func.getEntryPoint());
        println("Function size: " + func.getBody().getNumAddresses());

        DecompileResults res = decomp.decompileFunction(func, 30, monitor);
        if (res.decompileCompleted() && res.getDecompiledFunction() != null) {
            println("--- Decompiled C ---");
            println(res.getDecompiledFunction().getC());
        } else {
            println("Decompilation failed, showing disassembly");
            Address cur = func.getEntryPoint();
            Address end = func.getBody().getMaxAddress();
            int count = 0;
            while (cur != null && cur.compareTo(end) <= 0 && count < 60) {
                Instruction insn = listing.getInstructionAt(cur);
                if (insn != null) {
                    println("  " + cur + ": " + insn);
                    cur = cur.add(insn.getLength());
                } else { cur = cur.add(1); }
                count++;
            }
        }
    }
}
