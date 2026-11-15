class VerifyDynamic {
    public static void main(String[] args) throws ReflectiveOperationException {
        Object result = Class.forName("DynamicConstant").getMethod("value").invoke(null);
        if (result != null) {
            throw new AssertionError("Expected ConstantBootstraps.nullConstant to return null");
        }
    }
}
