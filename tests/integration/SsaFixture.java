import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

final class SsaFixture {
    @Target(ElementType.TYPE_USE)
    @Retention(RetentionPolicy.RUNTIME)
    @interface Mark {}

    static int trace;

    static int editable() {
        return 4;
    }

    static int step(int site, int failure) {
        trace = trace * 31 + site;
        if (site == failure) {
            switch (site) {
                case 1: throw new IllegalArgumentException();
                case 2: throw new ArithmeticException();
                default: throw new IllegalStateException();
            }
        }
        return site * 10;
    }

    static int protectedLocals(int failure) {
        int observed = 11;
        try {
            int a = step(1, failure);
            observed = 29;
            int b = step(2, failure);
            observed = 43;
            return a + b + step(3, failure);
        } catch (IllegalArgumentException | ArithmeticException ex) {
            trace = trace * 31 + 4;
            return observed;
        } catch (@Mark RuntimeException ex) {
            trace = trace * 31 + 5;
            return -observed;
        } finally {
            trace = trace * 31 + 6;
        }
    }

    static long rotate(int count, long left, long right) {
        for (int i = 0; i < count; ++i) {
            long old = left;
            left = right;
            right = old;
        }
        return left * 3 + right;
    }

    static int choose(int input) {
        switch (input) {
            case 1: case 2: return 7;
            case 3: return 11;
            case 4: return 15;
            default: return -1;
        }
    }

    static long update(long[] values, int index) {
        synchronized (values) {
            return values[index]++;
        }
    }

    static String stackJoin(int choose) {
        return "v=" + step(1, 0) + "/" + (choose == 0 ? step(2, 0) : step(3, 0));
    }
}
