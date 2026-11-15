public final class VerifyAnalysis {
    private static void verify(boolean condition) {
        if (!condition) {
            throw new AssertionError("rewritten class changed behavior");
        }
    }

    public static void main(String[] args) {
        new AnalysisFixture();
        verify(AnalysisFixture.interfaceJoin(true) == 7);
        verify(AnalysisFixture.interfaceJoin(false) == 11);
        verify(AnalysisFixture.ambiguousInterfaceJoin(true) == 3);
        verify(AnalysisFixture.ambiguousInterfaceJoin(false) == 2);
        verify(AnalysisFixture.independentInterfaceJoins(true, false) == 14);
        verify(AnalysisFixture.independentInterfaceJoins(false, true) == 12);
        verify(AnalysisFixture.arrays(true) == 2);
        verify(AnalysisFixture.arrays(false) == 3);
        verify(AnalysisFixture.wideLocals(true, Long.MAX_VALUE, 2L) == Long.MIN_VALUE + 3);
        verify(AnalysisFixture.wideLocals(false, 100L, 2L) == 6L);
        verify(AnalysisFixture.handlers(0) == -1);
        verify(AnalysisFixture.handlers(4) == 25);
        verify(AnalysisFixture.annotatedNew() instanceof StringBuilder);
        verify(AnalysisFixture.concatenate(42).equals("v=42"));
        verify(AnalysisFixture.dispatch(-1) == 1);
        verify(AnalysisFixture.dispatch(2) == 5);
        verify(AnalysisFixture.dispatch(3) == 8);
    }
}
