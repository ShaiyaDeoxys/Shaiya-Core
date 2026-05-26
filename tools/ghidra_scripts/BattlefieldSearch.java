// Search for battlefield.ini string references and analyze the loading code.
// @category Analysis

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.mem.*;
import java.util.*;

public class BattlefieldSearch extends GhidraScript {

    private List<Reference> getRefsTo(ReferenceManager refMgr, Address addr) {
        List<Reference> list = new ArrayList<>();
        var iter = refMgr.getReferencesTo(addr);
        while (iter.hasNext()) list.add(iter.next());
        return list;
    }

    private String readStringAt(Memory memory, Address addr) {
        StringBuilder sb = new StringBuilder();
        try {
            for (int i = 0; i < 512; i++) {
                byte b = memory.getByte(addr.add(i));
                if (b == 0) break;
                sb.append((char)(b & 0xFF));
            }
        } catch (Exception e) {}
        return sb.toString();
    }

    private Address findStringStart(Memory memory, Address addr) {
        Address start = addr;
        try {
            for (int back = 1; back <= 128; back++) {
                byte b = memory.getByte(addr.add(-back));
                if (b == 0) break;
                start = addr.add(-back);
            }
        } catch (Exception e) {}
        return start;
    }

    @Override
    public void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Listing listing = currentProgram.getListing();
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        println("=== Searching for battlefield strings ===");

        String[] patterns = { "battlefield", "BattleField", "Battlefield", "BATTLEFIELD" };
        Set<String> seen = new HashSet<>();

        for (String pat : patterns) {
            byte[] searchBytes = pat.getBytes("US-ASCII");
            Address addr = memory.findBytes(space.getAddress(0x400000), searchBytes, null, true, monitor);
            while (addr != null) {
                Address strStart = findStringStart(memory, addr);
                String key = strStart.toString();
                if (!seen.contains(key)) {
                    seen.add(key);
                    String fullStr = readStringAt(memory, strStart);
                    println("\nString at " + strStart + ": \"" + fullStr + "\"");

                    // Find refs to the string start
                    List<Reference> refs = getRefsTo(refMgr, strStart);
                    if (refs.isEmpty()) {
                        println("  No direct references to string start.");
                    }
                    for (Reference ref : refs) {
                        Address fromAddr = ref.getFromAddress();
                        Function func = listing.getFunctionContaining(fromAddr);
                        String funcName = func != null ? func.getName() + " @ " + func.getEntryPoint() : "no function";
                        println("  Ref from " + fromAddr + " in " + funcName);
                        disasmAround(listing, fromAddr, 30);
                    }

                    // Also check refs to the match address if different
                    if (!strStart.equals(addr)) {
                        List<Reference> refs2 = getRefsTo(refMgr, addr);
                        for (Reference ref : refs2) {
                            Address fromAddr = ref.getFromAddress();
                            Function func = listing.getFunctionContaining(fromAddr);
                            String funcName = func != null ? func.getName() + " @ " + func.getEntryPoint() : "no function";
                            println("  Ref (mid-string) from " + fromAddr + " in " + funcName);
                        }
                    }
                }

                addr = memory.findBytes(addr.add(1), searchBytes, null, true, monitor);
            }
        }

        println("\n=== Done ===");
    }

    private void disasmAround(Listing listing, Address center, int count) {
        // Walk backward to find a good start
        Address start = center;
        try {
            for (int back = 0; back < 40; back++) {
                Instruction insn = listing.getInstructionContaining(center.add(-back));
                if (insn != null) {
                    start = insn.getAddress();
                    // Keep going back a bit more
                    for (int i = 0; i < 8; i++) {
                        Instruction prev = listing.getInstructionBefore(start);
                        if (prev != null) start = prev.getAddress();
                        else break;
                    }
                    break;
                }
            }
        } catch (Exception e) {}

        Address cur = start;
        for (int i = 0; i < count && cur != null; i++) {
            Instruction insn = listing.getInstructionAt(cur);
            if (insn != null) {
                String marker = cur.equals(center) ? " <<<" : "";
                println("    " + cur + ": " + insn.toString() + marker);
                cur = cur.add(insn.getLength());
            } else {
                cur = cur.add(1);
            }
        }
    }
}
