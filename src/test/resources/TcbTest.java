public final class TcbTest {
    public static void main(String[] args) {
        for (int i = 0; i < 4; i++) {
            try {
                switch (i) {
                    case 0: throw new NullPointerException();
                    case 1: throw new IllegalStateException();
                    case 2: throw new IndexOutOfBoundsException();
                    case 3: throw new IllegalArgumentException();
                }
            } catch (NullPointerException nullPointerException) {
                System.out.println("Null Pointer");
            } catch (IllegalStateException illegalStateException) {
                System.out.println("Illegal State");
            } catch (IndexOutOfBoundsException | IllegalArgumentException e) {
                System.out.println("Compound");
                try {
                    if (e instanceof IllegalArgumentException) {
                        System.out.println("start inner");
                        ((Object)null).toString();
                        System.out.println("Shouldn't happen");
                    }
                } catch (NullPointerException e1) {
                    System.out.println("exception in handler");
                }
            }
        }
    }
}