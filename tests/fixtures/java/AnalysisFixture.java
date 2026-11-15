import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

public final class AnalysisFixture {
    @Target(ElementType.TYPE_USE)
    @Retention(RetentionPolicy.RUNTIME)
    public @interface Marked {}

    interface Score {
        int value();
    }

    static final class Left implements Score {
        public int value() { return 7; }
    }

    static final class Right implements Score {
        public int value() { return 11; }
    }

    interface First {
        int first();
    }

    interface Second {
        int second();
    }

    static final class BothLeft implements First, Second {
        public int first() { return 3; }
        public int second() { return 5; }
    }

    static final class BothRight implements First, Second {
        public int first() { return 7; }
        public int second() { return 11; }
    }

    static int interfaceJoin(boolean left) {
        Score score;
        if (left) {
            score = new Left();
        } else {
            score = new Right();
        }
        return score.value();
    }

    static int ambiguousInterfaceJoin(boolean left) {
        CharSequence value = left ? "abc" : new StringBuilder("de");
        return value.length();
    }

    static int independentInterfaceJoins(boolean first, boolean second) {
        BothLeft left = new BothLeft();
        BothRight right = new BothRight();
        return (first ? (First) left : right).first() + (second ? (Second) left : right).second();
    }

    static int arrays(boolean left) {
        Object[] values = left ? new String[2] : new Number[3];
        return values.length;
    }

    static long wideLocals(boolean left, long first, long second) {
        long value = left ? first : second;
        return value + 4L;
    }

    static int handlers(int value) {
        try {
            return 100 / value;
        } catch (@Marked ArithmeticException exception) {
            return -1;
        } catch (IllegalArgumentException exception) {
            return -2;
        }
    }

    static Object annotatedNew() {
        return new @Marked StringBuilder();
    }

    static String concatenate(int value) {
        return "v=" + value;
    }

    static int dispatch(int value) {
        switch (value) {
            case -1: return 1;
            case 0: return 2;
            case 1: return 3;
            case 2: return 5;
            default: return 8;
        }
    }
}
