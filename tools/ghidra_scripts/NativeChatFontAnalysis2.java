// Deep dive: decompile font creation (FUN_005762d0), text draw (FUN_0057d570),
// DrawText_ViewPoint impl (FUN_00573ee0), and color assignment (FUN_004207f0)
// @category Analysis

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;

public class NativeChatFontAnalysis2 extends GhidraScript {

    @Override
    public void run() throws Exception {
        Listing listing = currentProgram.getListing();
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        // 1. Font creation — uses "Arial" and "Verdana"
        println("========================================");
        println("=== 1. Font creation FUN_005762d0 ===");
        decompileAt(listing, decomp, space, 0x5762d0);

        // 2. Actual text draw function — called by 0x573C00
        println("\n========================================");
        println("=== 2. Text draw FUN_0057d570 ===");
        decompileAt(listing, decomp, space, 0x57d570);

        // 3. DrawText_ViewPoint impl
        println("\n========================================");
        println("=== 3. DrawText_ViewPoint impl FUN_00573ee0 ===");
        decompileAt(listing, decomp, space, 0x573ee0);

        // 4. Chat color assignment — called per chat line
        println("\n========================================");
        println("=== 4. Chat color FUN_004207f0 ===");
        decompileAt(listing, decomp, space, 0x4207f0);

        // 5. Text width calc FUN_00575740 (used in word wrap for 0x145 comparison)
        println("\n========================================");
        println("=== 5. Text width FUN_00575740 ===");
        decompileAt(listing, decomp, space, 0x575740);

        // 6. The chat line renderer — likely in the big function 0x4344c0 that calls 0x573C00
        // Let's also look at FUN_00420c10 which is called to prep text before display
        println("\n========================================");
        println("=== 6. Text prep FUN_00420c10 ===");
        decompileAt(listing, decomp, space, 0x420c10);

        // 7. Tahoma reference in FUN_006152a0 — might be a secondary font
        println("\n========================================");
        println("=== 7. Tahoma font FUN_006152a0 ===");
        decompileAt(listing, decomp, space, 0x6152a0);

        decomp.dispose();
        println("\n=== Done ===");
    }

    void decompileAt(Listing listing, DecompInterface decomp, AddressSpace space, long addr) throws Exception {
        Address target = space.getAddress(addr);
        Function func = listing.getFunctionAt(target);
        if (func == null)
            func = listing.getFunctionContaining(target);

        if (func == null) {
            println("No function at/containing 0x" + Long.toHexString(addr));
            Address cur = target;
            for (int i = 0; i < 60; i++) {
                Instruction insn = listing.getInstructionAt(cur);
                if (insn != null) {
                    println("  " + cur + ": " + insn);
                    cur = cur.add(insn.getLength());
                } else {
                    cur = cur.add(1);
                }
            }
            return;
        }

        println("Function: " + func.getName() + " at " + func.getEntryPoint());
        println("Size: " + func.getBody().getNumAddresses() + " bytes");

        DecompileResults res = decomp.decompileFunction(func, 60, monitor);
        if (res.decompileCompleted() && res.getDecompiledFunction() != null) {
            println("--- Decompiled C ---");
            println(res.getDecompiledFunction().getC());
        } else {
            println("Decompilation failed");
            Address cur = func.getEntryPoint();
            Address end = func.getBody().getMaxAddress();
            int count = 0;
            while (cur != null && cur.compareTo(end) <= 0 && count < 80) {
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
