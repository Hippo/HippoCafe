public class TableSwitchTest {
    public static void main(String[] args) {
        double test = 0;
        for (int i = 0; i < 2; i++) {
            test += i * 0.5;
        }
        test += 1;
        switch ((int) test) {
            case 1:
                System.out.println("1 case");
                break;
            case 3:
                System.out.println("3 case");
                break;
            case 5:
                System.out.println("5 case");
        }
    }
}