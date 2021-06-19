public final class InvokeDynamicTest {

    public static void main(String[] args) {
        run(() -> System.out.println("Hello from dynamic"));
    }

    private static void run(Runnable runnable) {
        runnable.run();
    }
}