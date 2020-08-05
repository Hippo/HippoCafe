import java.util.Random;

public final class BranchTest {
    
    public static void main(String[] args) {
        if (new Random().nextBoolean()) {
            System.out.println("True");
        } else {
            System.out.println("False");
        }
    }
}
