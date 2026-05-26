// Decompile the native text rendering functions used by the Shaiya chat system
// to understand font creation, shadow technique, and rendering parameters.
// @category Analysis

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class NativeChatFontAnalysis extends GhidraScript {

    @Override
    public void run() throws Exception {
        Listing listing = currentProgram.getListing();
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        // =====================================================================
        // 1. DrawText_ViewPoint wrapper at 0x531640
        //    Signature: void __cdecl(int x, int y, D3DCOLOR color, const char* text)
        // =====================================================================
        println("========================================");
        println("=== 1. DrawText_ViewPoint at 0x531640 ===");
        decompileAt(listing, decomp, space, 0x531640);

        // =====================================================================
        // 2. Low-level text renderer at 0x573C00
        //    This is the function that actually puts pixels on screen.
        //    Both the chat system and emoji probe hook into this.
        // =====================================================================
        println("\n========================================");
        println("=== 2. Low-level text renderer at 0x573C00 ===");
        decompileAt(listing, decomp, space, 0x573C00);

        // =====================================================================
        // 3. ChatBox add line at 0x422630
        //    This is where chat messages enter the native chatbox buffer.
        // =====================================================================
        println("\n========================================");
        println("=== 3. ChatBox add line at 0x422630 ===");
        decompileAt(listing, decomp, space, 0x422630);

        // =====================================================================
        // 4. ChatBox add token (called before add line) at 0x422B90
        //    This processes chat text tokens before rendering.
        // =====================================================================
        println("\n========================================");
        println("=== 4. ChatBox token processor at 0x422B90 ===");
        decompileAt(listing, decomp, space, 0x422B90);

        // =====================================================================
        // 5. Chat panel init at 0x47D1F0
        //    Where the chat panel object is created — may reveal font init.
        // =====================================================================
        println("\n========================================");
        println("=== 5. Chat panel init at 0x47D1F0 ===");
        decompileAt(listing, decomp, space, 0x47D1F0);

        // =====================================================================
        // 6. Search for D3DXCreateFont calls to find chat font creation
        // =====================================================================
        println("\n========================================");
        println("=== 6. Searching for D3DXCreateFont references ===");
        Memory memory = currentProgram.getMemory();

        // Search for common font-related strings
        String[] fontStrings = { "Arial", "Tahoma", "Verdana", "MS Sans", "Gulim" };
        for (String fontStr : fontStrings) {
            byte[] searchBytes = fontStr.getBytes("US-ASCII");
            Address found = memory.findBytes(space.getAddress(0x700000), searchBytes, null, true, monitor);
            while (found != null && found.getOffset() < 0x800000) {
                // Check if this is a proper string start
                byte prevByte = 0;
                try { prevByte = memory.getByte(found.add(-1)); } catch (Exception e) {}
                if (prevByte == 0 || found.getOffset() == 0x700000) {
                    StringBuilder sb = new StringBuilder();
                    for (int i = 0; i < 64; i++) {
                        byte b = memory.getByte(found.add(i));
                        if (b == 0) break;
                        sb.append((char)(b & 0xFF));
                    }
                    println("Font string at " + found + ": \"" + sb.toString() + "\"");

                    // Check references to this address
                    var refs = refMgr.getReferencesTo(found);
                    while (refs.hasNext()) {
                        Reference ref = refs.next();
                        Address fromAddr = ref.getFromAddress();
                        Function func = listing.getFunctionContaining(fromAddr);
                        String funcName = func != null ? func.getName() + " @ " + func.getEntryPoint() : "no function";
                        println("  Ref from " + fromAddr + " in " + funcName);
                    }
                }
                found = memory.findBytes(found.add(1), searchBytes, null, true, monitor);
            }
        }

        // =====================================================================
        // 7. Look at DrawText_ChatBox at 0x4231A0 — the chat-specific renderer
        // =====================================================================
        println("\n========================================");
        println("=== 7. DrawText_ChatBox at 0x4231A0 ===");
        decompileAt(listing, decomp, space, 0x4231A0);

        // =====================================================================
        // 8. Decompile the chatbox render function — walk from 0x422630 callers
        // =====================================================================
        println("\n========================================");
        println("=== 8. Functions calling 0x422630 (chatbox add line) ===");
        Address chatAddLine = space.getAddress(0x422630);
        var callers = refMgr.getReferencesTo(chatAddLine);
        while (callers.hasNext()) {
            Reference ref = callers.next();
            if (ref.getReferenceType().isCall()) {
                Address fromAddr = ref.getFromAddress();
                Function func = listing.getFunctionContaining(fromAddr);
                if (func != null) {
                    println("Called from " + fromAddr + " in " + func.getName() + " @ " + func.getEntryPoint());
                }
            }
        }

        // =====================================================================
        // 9. Look at what calls 0x573C00 (low-level text draw)
        // =====================================================================
        println("\n========================================");
        println("=== 9. Functions calling 0x573C00 (text draw) ===");
        Address textDraw = space.getAddress(0x573C00);
        var textDrawCallers = refMgr.getReferencesTo(textDraw);
        int callCount = 0;
        while (textDrawCallers.hasNext() && callCount < 30) {
            Reference ref = textDrawCallers.next();
            Address fromAddr = ref.getFromAddress();
            Function func = listing.getFunctionContaining(fromAddr);
            String funcName = func != null ? func.getName() + " @ " + func.getEntryPoint() : "no function";
            println("  " + ref.getReferenceType() + " from " + fromAddr + " in " + funcName);
            callCount++;
        }

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
            // Show disassembly instead
            Address cur = target;
            for (int i = 0; i < 50; i++) {
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
            println("Decompilation failed: " + (res.getErrorMessage() != null ? res.getErrorMessage() : "unknown"));
            // Show disassembly
            Address cur = func.getEntryPoint();
            Address end = func.getBody().getMaxAddress();
            int count = 0;
            while (cur != null && cur.compareTo(end) <= 0 && count < 80) {
                Instruction insn = listing.getInstructionAt(cur);
                if (insn != null) {
                    println("  " + cur + ": " + insn);
                    cur = cur.add(insn.getLength());
                } else {
                    cur = cur.add(1);
                }
                count++;
            }
        }
    }
}
