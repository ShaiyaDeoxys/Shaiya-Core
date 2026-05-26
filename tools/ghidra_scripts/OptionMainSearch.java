// Search for "option_main" strings in the binary and dump their addresses,
// sections, and cross-references to understand the options menu lag.
//@category Search

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import java.util.ArrayList;
import java.util.List;

public class OptionMainSearch extends GhidraScript {

    @Override
    public void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Listing listing = currentProgram.getListing();

        byte[] pattern = "option_main".getBytes("US-ASCII");

        println("=== Searching for 'option_main' in all memory blocks ===");

        for (MemoryBlock block : memory.getBlocks()) {
            Address start = block.getStart();
            Address end = block.getEnd();

            println(String.format("Block: %-12s  %s - %s  size=0x%X  perms=%s%s%s",
                block.getName(), start, end, block.getSize(),
                block.isRead() ? "R" : "-",
                block.isWrite() ? "W" : "-",
                block.isExecute() ? "X" : "-"));

            Address found = memory.findBytes(start, end, pattern, null, true, monitor);
            while (found != null) {
                // Read the full null-terminated string at this location
                StringBuilder sb = new StringBuilder();
                Address reader = found;
                // Read backwards to find start of string
                Address strStart = found;
                try {
                    Address back = found.subtract(1);
                    while (back.compareTo(start) >= 0) {
                        byte b = memory.getByte(back);
                        if (b < 0x20 || b > 0x7E) break;
                        strStart = back;
                        back = back.subtract(1);
                    }
                } catch (Exception e) {}

                // Read forward from start
                try {
                    Address fwd = strStart;
                    while (fwd.compareTo(end) <= 0) {
                        byte b = memory.getByte(fwd);
                        if (b == 0) break;
                        if (b < 0x20 || b > 0x7E) break;
                        sb.append((char) b);
                        fwd = fwd.add(1);
                    }
                } catch (Exception e) {}

                String fullString = sb.toString();
                println(String.format("  FOUND at %s (block=%s): \"%s\"",
                    strStart, block.getName(), fullString));

                // Look for references (xrefs) to this address
                ReferenceManager refMgr = currentProgram.getReferenceManager();
                ReferenceIterator refIter = refMgr.getReferencesTo(strStart);
                List<Reference> refs = new ArrayList<>();
                while (refIter.hasNext()) refs.add(refIter.next());

                if (!refs.isEmpty()) {
                    for (Reference ref : refs) {
                        Address from = ref.getFromAddress();
                        Function fn = listing.getFunctionContaining(from);
                        String fnName = fn != null ? fn.getName() : "<no function>";
                        println(String.format("    XREF from %s  (fn=%s, type=%s)",
                            from, fnName, ref.getReferenceType()));
                    }
                } else {
                    println("    (no direct xrefs found - may be loaded indirectly)");
                }

                // Find next occurrence
                Address next = found.add(1);
                if (next.compareTo(end) > 0) break;
                found = memory.findBytes(next, end, pattern, null, true, monitor);
            }
        }

        // Also search for the .tga/.png variants specifically
        println("\n=== Searching for full filenames ===");
        String[] targets = {
            "option_main.tga", "option_main.png",
            "option_main_button.tga", "option_main_button.png",
            "option_main_button2.tga", "option_main_button2.png"
        };

        for (String target : targets) {
            byte[] tgt = target.getBytes("US-ASCII");
            boolean found2 = false;
            for (MemoryBlock block : memory.getBlocks()) {
                Address f = memory.findBytes(block.getStart(), block.getEnd(), tgt, null, true, monitor);
                while (f != null) {
                    found2 = true;
                    println(String.format("  \"%s\" at %s (block=%s, exec=%s)",
                        target, f, block.getName(), block.isExecute()));

                    Address next = f.add(1);
                    if (next.compareTo(block.getEnd()) > 0) break;
                    f = memory.findBytes(next, block.getEnd(), tgt, null, true, monitor);
                }
            }
            if (!found2) {
                println(String.format("  \"%s\" NOT FOUND", target));
            }
        }
    }
}
