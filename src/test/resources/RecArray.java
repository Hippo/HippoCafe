import java.util.ArrayList;

public class RecArray {

    public static void main(String[] args) {
        for (int i = 0; i < 5; i++) {
            ArrayList<Integer> list = new ArrayList<>();
            for (int j = 0; j < 5; j++) {
                list.add(j * i);
            }
            System.out.println(list);
        }
    }
}