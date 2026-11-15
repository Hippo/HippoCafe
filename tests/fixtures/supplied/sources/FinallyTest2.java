public final class FinallyTest2 {
  public static void main(String[] args) {
    try {
      System.out.println("In tcb");
    } catch (Exception e) {
      e.printStackTrace();
    } finally {
      System.out.println("Finally block");
    }
  }
}