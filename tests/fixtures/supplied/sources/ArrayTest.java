public class ArrayTest {

    private static long[][] globalWideMultiArray = new long[1][];
    private static String[][] globalObjArray = new String[1][];

    public static void main(String[] args) {
        String[] local = new String[1];
        int[][] localMultiPrim = new int[1][];
        String[][] idekAnyMore = new String[1][1];

        globalWideMultiArray[0] = new long[] {12};
        globalObjArray[0] = new String[] {"Wow Coool Array"};
        local[0] = "2*2e,1e2*7.6";
        localMultiPrim[0] = new int[] {69};
        idekAnyMore[0][0] = "Ayo Multi anew array";

        for (long i = 0; i < globalWideMultiArray[0][0]; i++) {
            char[] chars = local[0].toCharArray();
            chars[(int) i] ^= localMultiPrim[0][0];
            local[0] = String.valueOf(chars);
        }
        System.out.println(local[0] + " " + globalObjArray[0][0]);
        System.out.println(idekAnyMore[0][0]);
    }

}