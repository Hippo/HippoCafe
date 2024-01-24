
import java.beans.Transient;
import java.lang.annotation.*;
import java.lang.reflect.Field;
import java.lang.reflect.Method;

public final class AnnotationTest {


    public static enum TestElement {
        FIRST, SECOND
    }

    @Retention(RetentionPolicy.RUNTIME)
    @Target(ElementType.METHOD)
    public static @interface CoolAnnotation {
        String test();
        boolean test1();
        String[] arrayTest();
        TestElement enumTest();
    }

    @Deprecated
    private final String depField = "This field is deprecated";

    public static void main(String[] args) {
        try {
            for (Field declaredField : AnnotationTest.class.getDeclaredFields()) {
                for (Annotation annotation : declaredField.getAnnotations()) {
                    System.out.println(declaredField + " -> " + annotation);
                }
            }

            for (Method declaredMethod : AnnotationTest.class.getDeclaredMethods()) {
                for (Annotation annotation : declaredMethod.getAnnotations()) {
                    System.out.println(declaredMethod + " -> " + annotation);
                }
                for (Annotation[] parameterAnnotation : declaredMethod.getParameterAnnotations()) {
                    for (Annotation annotation : parameterAnnotation) {
                        System.out.println("(parameter) " + declaredMethod + " -> " + annotation);
                    }
                }
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    @Transient(value = false)
    @CoolAnnotation(test = "Wow so cool", test1 = false, arrayTest = {"element1", "element2", "element3"}, enumTest = TestElement.SECOND)
    public void coolMethod(@Deprecated String param) {
        System.out.println(param);
    }
}