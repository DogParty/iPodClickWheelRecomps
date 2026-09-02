import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import ghidra.app.decompiler.*;
import java.io.*;
import java.util.*;

public class DumpFuncs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : "/tmp/funcs";
        PrintWriter fw = new PrintWriter(new FileWriter(out + "/functions.tsv"));
        PrintWriter cw = new PrintWriter(new FileWriter(out + "/decomp.c"));
        fw.println("addr\tsize\tname\tcallers\tcallees\tinsns\tthunk");
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        int n = 0, total_insn = 0;
        while (it.hasNext()) {
            Function f = it.next();
            AddressSetView body = f.getBody();
            long size = body.getNumAddresses();
            int insns = 0;
            InstructionIterator ii = currentProgram.getListing().getInstructions(body, true);
            while (ii.hasNext()) { ii.next(); insns++; }
            total_insn += insns;
            Set<Function> callers = f.getCallingFunctions(monitor);
            Set<Function> callees = f.getCalledFunctions(monitor);
            fw.println(f.getEntryPoint() + "\t" + size + "\t" + f.getName() + "\t" + callers.size() + "\t" + callees.size() + "\t" + insns + "\t" + f.isThunk());
            DecompileResults r = ifc.decompileFunction(f, 60, monitor);
            cw.println("// ==== " + f.getEntryPoint() + " " + f.getName() + " size=" + size);
            if (r != null && r.decompileCompleted() && r.getDecompiledFunction() != null)
                cw.println(r.getDecompiledFunction().getC());
            else cw.println("// DECOMPILE FAILED: " + (r == null ? "null" : r.getErrorMessage()));
            n++;
        }
        fw.close(); cw.close();
        println("functions=" + n + " total_insns=" + total_insn);
    }
}
