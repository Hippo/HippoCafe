
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.util.ArrayList;

public final class LocalAnnotation {

    public static enum TestElement {
        FIRST, SECOND
    }

    @Retention(RetentionPolicy.RUNTIME)
    @Target(ElementType.TYPE_USE)
    public static @interface CoolAnnotation {

    }

    public static void main(String[] args) {
        ArrayList<Integer> integers = new @CoolAnnotation ArrayList<>();
        integers.add(1);
        System.out.println(integers);
    }
}
