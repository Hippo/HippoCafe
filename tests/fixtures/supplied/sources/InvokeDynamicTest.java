public final class InvokeDynamicTest {

    public static void main(String[] args) {
        invoke(() -> System.out.println("Hello from dynamic"));
    }

    private static void invoke(Runnable runnable) {
        runnable.run();
    }
}