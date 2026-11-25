import java.lang.reflect.Method;

public final class VerifyGenerated {
    public static void main(String[] args) throws Exception {
        Class<?> generated = Class.forName("GeneratedFrames");
        Method branch = generated.getMethod("wideBranch", boolean.class);
        Method allocation = generated.getMethod("allocated", boolean.class);
        Method array = generated.getMethod("array", boolean.class);
        generated.getMethod("loop", int.class).invoke(null, 3);
        if (!branch.invoke(null, true).equals(1) || !branch.invoke(null, false).equals(2)
                || !(allocation.invoke(null, true) instanceof StringBuilder)
                || !(allocation.invoke(null, false) instanceof StringBuilder)
                || ((int[]) array.invoke(null, true)).length != 1
                || ((int[]) array.invoke(null, false)).length != 2
                || !generated.getMethod("wideConstant").invoke(null).equals(1234)
                || !(generated.getMethod("initializeWithHandler").invoke(null) instanceof StringBuilder)) {
            throw new AssertionError("generated class changed behavior");
        }
        Class<?> symbolic = Class.forName("SymbolicInstructions");
        if (!symbolic.getMethod("concat", int.class).invoke(null, -9).equals("9:2147483647:checked")
                || !symbolic.getMethod("wide").invoke(null).equals(1234567890123L)) {
            throw new AssertionError("symbolic instructions returned unexpected values");
        }
        Class<?> entries = Class.forName("BranchEntries");
        Method preserve = entries.getMethod("preserve", int.class);
        Method include = entries.getMethod("include", int.class);
        Method ordered = entries.getMethod("ordered", int.class);
        // 0 takes ifeq, 1 uses a switch case, 2 falls through the patch, and 3 takes the default.
        for (int input = 0; input <= 3; input++) {
            int preserved = input == 2 ? input + 5 : input;
            if (!preserve.invoke(null, input).equals(preserved) || !include.invoke(null, input).equals(input + 5)
                    || !ordered.invoke(null, input).equals(input + 5)) {
                throw new AssertionError("unexpected insertion entry for input " + input);
            }
        }
        Method preserveBounds = entries.getMethod("preserveBounds", int.class);
        Method includeBounds = entries.getMethod("includeBounds", int.class);
        for (Method method : new Method[]{preserveBounds, includeBounds}) {
            // A negative input throws inside the original try. Dispatch skips the insertion.
            // A positive input reaches the insertion by fallthrough and visits it once.
            if (!method.invoke(null, -1).equals(0) || !method.invoke(null, 1).equals(1)) {
                throw new AssertionError("wrong exception or fallthrough entry in " + method.getName());
            }
        }
        // A zero input jumps to the shared exclusive end and handler target. Redirecting
        // that jump executes the insertion, whose division by zero is still inside the try.
        if (!preserveBounds.invoke(null, 0).equals(0) || !includeBounds.invoke(null, 0).equals(1)) {
            throw new AssertionError("wrong protected interval after redirecting an ordinary branch");
        }
    }
}
