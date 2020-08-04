
public final class SwitchTest implements java.util.function.Predicate<String> {

    @Deprecated
    @InnerAnnotation(test = 69)
    private static String testField = "ur black";

    public boolean test(String test) {
        return true;
    }

    public static void main(String[] args) {
        int local = 6;
        try {
            System.out.println(new SwitchTest().test(testField));
        }catch (Exception e) {
            local = 5;
        }

        switch (local) {
            case 6:
                System.out.println("ye");
                break;
            case 5:
                System.out.println("ya");
                break;
            default:
                System.out.println("default");
        }

        System.out.println(local);
    }

    public static @interface InnerAnnotation {
        int test();
    }
}
