import java.util.Arrays;

public final class ParameterFrameTest {

  public static void main(String[] args) {
    System.out.println(testMe("random"));
    String[] strings = {"dummylol", "random"};
    System.out.println(Arrays.toString(testMeButAnArray(strings)));
  }

  public static String testMe(String a) {
    if (a.startsWith("dummy")) {
      return null;
    }
    return a.concat("congrats");
  }

  public static String[] testMeButAnArray(String[] a) {
    for (int i = 0; i < a.length; i++) {
      String s = a[i];
      if (s.startsWith("dummy")) {
        a[i] = null;
      } else {
        a[i] = s.concat("congrats");
      }
    }
    return a;
  }
}