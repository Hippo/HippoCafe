import java.util.*;

public class LongInitTest {

    private String test;
    private Map<String, Long> map;
    private long time;

    public LongInitTest(String test) {
        this.test = test;
        this.map = new HashMap<>();
        this.time = System.currentTimeMillis();
    }

    public static void main(String[] args) {
        LongInitTest test = new LongInitTest("test");
        System.out.println(test.test);
        System.out.println(test.map);
        System.out.println(test.time);
    }
}