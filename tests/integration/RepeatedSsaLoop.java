final class RepeatedSsaLoop {
    static int sum(int n) {
        int total = 0;
        while (n > 0) {
            total += n;
            n--;
        }
        return total;
    }
}

final class VerifyRepeatedSsaLoop {
    private static int expectedN, expectedTotal, observations;

    // Called only from a copy instrumented after round five, using the two slots
    // advertised to a debugger at the loop condition.
    static void observe(int n, int total) {
        if (n != expectedN || total != expectedTotal)
            throw new AssertionError("debug locals at loop condition: n=" + n + ", total=" + total);
        expectedN--;
        expectedTotal += 2;
        observations++;
    }

    public static void main(String[] args) {
        for (int n : new int[]{-3, 0, 1, 4, 20}) {
            expectedN = n;
            expectedTotal = observations = 0;
            int count = Math.max(0, n);
            int expected = args[0].equals("original") ? count * (count + 1) / 2 : 2 * count;
            int actual = RepeatedSsaLoop.sum(n);
            if (actual != expected)
                throw new AssertionError("sum(" + n + ") was " + actual + ", expected " + expected);
            if (args[0].equals("probe") && observations != count + 1)
                throw new AssertionError("missed a loop condition");
        }
    }
}
