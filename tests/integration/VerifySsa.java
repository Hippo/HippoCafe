import java.io.PrintStream;
import java.lang.invoke.CallSite;
import java.lang.invoke.ConstantCallSite;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;

public final class VerifySsa {
    private static int constantLinks;
    private static int invocationLinks;

    public static int constant(MethodHandles.Lookup lookup, String name, Class<?> type) {
        return ++constantLinks;
    }

    public static CallSite invocation(MethodHandles.Lookup lookup, String name, MethodType type) {
        return new ConstantCallSite(MethodHandles.constant(int.class, ++invocationLinks));
    }

    public static void main(String[] args) throws Exception {
        if (args[0].equals("edited")) {
            if (SsaFixture.editable() != 5
                    || (int) SsaFixture.class.getDeclaredMethod("authored").invoke(null) != 2) {
                throw new AssertionError("SSA-edited and SSA-authored methods must return their new values");
            }
            return;
        }
        try (PrintStream output = new PrintStream(args[0], StandardCharsets.UTF_8)) {
            trace(output);
            for (int i = 1; i < args.length; ++i) {
                traceProtectedLoop(output, args[i]);
            }
            if (output.checkError()) {
                throw new AssertionError("could not write execution trace");
            }
        }
    }

    static void traceProtectedLoop(PrintStream output, String fixture) throws Exception {
        Method run = Class.forName(fixture).getMethod("run", int.class, int.class, StringBuilder.class);
        for (int trips : new int[]{0, 1, 4}) {
            // Zero completes normally. Every other input throws at one specific iteration and call.
            for (int failure = 0; failure <= 2 * trips; ++failure) {
                StringBuilder events = new StringBuilder();
                int result = (int) run.invoke(null, trips, failure, events);
                boolean arithmetic = events.indexOf("A") >= 0;
                boolean runtime = events.indexOf("R") >= 0;
                if (arithmetic != (failure % 2 == 1) || runtime != (failure > 0 && failure % 2 == 0)) {
                    throw new AssertionError(fixture + " selected the wrong handler for failure " + failure);
                }
                output.println(fixture + "/" + trips + "/" + failure + "/" + result + "/" + events);
            }
        }
    }

    static void trace(PrintStream output) throws Exception {
        for (int i = 0; i < 5; ++i) {
            SsaFixture.trace = 0;
            output.println(SsaFixture.protectedLocals(i) + "/" + SsaFixture.trace);
            output.println(SsaFixture.rotate(i, 0x1122334455667788L, Long.MIN_VALUE));
            output.println(SsaFixture.choose(i));
            SsaFixture.trace = 0;
            output.println(SsaFixture.stackJoin(i) + "/" + SsaFixture.trace);
        }
        long[] values = {Long.MAX_VALUE, 123};
        output.println(SsaFixture.update(values, 0) + "/" + values[0]);
        try {
            SsaFixture.update(values, 2);
            throw new AssertionError("expected out-of-bounds access");
        } catch (IndexOutOfBoundsException expected) {
            StackTraceElement location = expected.getStackTrace()[0];
            output.println(expected.getClass().getName() + "/" + Thread.holdsLock(values)
                    + "/" + location.getMethodName() + ":" + location.getLineNumber());
        }
        Class<?> linkage = Class.forName("SsaLinkage");
        for (String name : new String[]{"read", "call"}) {
            try {
                linkage.getMethod(name).invoke(null);
                throw new AssertionError("expected a linkage error executing " + name);
            } catch (java.lang.reflect.InvocationTargetException thrown) {
                if (thrown.getCause().getClass() != IncompatibleClassChangeError.class) {
                    throw thrown;
                }
                output.println(name + "/" + thrown.getCause().getClass().getName());
            }
        }
        output.println(Class.forName("SsaDead").getMethod("answer").invoke(null));
        Method walk = Class.forName("SsaTwoEntryLoop").getMethod("walk", boolean.class, int.class);
        for (boolean startRight : new boolean[]{false, true}) {
            for (int trips = 1; trips <= 6; ++trips) {
                int actual = (int) walk.invoke(null, startRight, trips);
                int expected = (trips / 2) * 8 + (trips % 2) * (startRight ? 5 : 3);
                if (actual != expected) {
                    throw new AssertionError("incorrect loop-carried value " + actual);
                }
                output.println(actual);
            }
        }
        Class<?> dynamic = Class.forName("SsaDynamic");
        Method constants = dynamic.getMethod("constants");
        Method calls = dynamic.getMethod("calls");
        for (int i = 0; i < 2; ++i) {
            int loaded = (int) constants.invoke(null);
            int invoked = (int) calls.invoke(null);
            if (loaded != 4 || invoked != 3 || constantLinks != 2 || invocationLinks != 2) {
                throw new AssertionError("dynamic linkage identity changed");
            }
            output.println(loaded + "/" + invoked + "/" + constantLinks + "/" + invocationLinks);
        }
    }
}
