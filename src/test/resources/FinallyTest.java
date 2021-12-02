public final class FinallyTest {
  public static void main(String[] args) {
    try {
      throw new RuntimeException("Wow this is cool");
    } catch (Exception e) {
      e.printStackTrace();
    } finally {
      System.out.println("Finally block");
    }
  }
}