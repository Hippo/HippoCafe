import java.util.Random;

public final class JumpInit {
  private JumpInit() {
    if (new Random().nextBoolean()) {
      System.out.println("True");
    } else {
      System.out.println("False");
    }
  }

  public static void main(String[] args) {
    new JumpInit();
  }
}