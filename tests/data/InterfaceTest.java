
public final class InterfaceTest implements java.util.function.Predicate<String> {

    @Deprecated
    @InnerAnnotation(test = 69)
    private static String testField = "field";

    public boolean test(String test) {
        return true;
    }

    public static void main(String[] args) {
        int local = 6;
        try {
            System.out.println(new InterfaceTest().test(testField));
        }catch (Exception e) {
            local = 5;
        }
        System.out.println(local);
    }

    public static @interface InnerAnnotation {
        int test();
    }
}
