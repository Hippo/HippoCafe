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
    }
}
