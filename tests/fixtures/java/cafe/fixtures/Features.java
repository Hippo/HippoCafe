package cafe.fixtures;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.util.function.IntSupplier;

@Retention(RetentionPolicy.RUNTIME)
@Target({ElementType.TYPE, ElementType.TYPE_USE, ElementType.METHOD,
         ElementType.PARAMETER, ElementType.RECORD_COMPONENT})
@interface Marker {
    String value() default "default";
}

@Marker
public final class Features {
    public sealed interface Choice permits Number, Empty {}

    public record Number(@Marker int number) implements Choice {}

    public enum Empty implements Choice { INSTANCE }

    public static @Marker int value(@Marker Choice choice) {
        IntSupplier compute = () -> {
            if (choice instanceof Number number) {
                return number.number();
            }
            return -1;
        };
        try {
            return compute.getAsInt();
        } catch (RuntimeException error) {
            return 0;
        }
    }
}
