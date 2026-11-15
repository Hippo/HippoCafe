import java.util.*;
import java.util.function.Function;
import java.util.stream.Collectors;

public class CalculationTest {

    public static void main(String[] args) {
        try {
            Map<String, List<Integer>> dataMap = new HashMap<>();
            dataMap.put("First", Arrays.asList(1, 2, 3));
            dataMap.put("Second", Arrays.asList(4, 5, 6));

            int calculationResult = complexDataProcessing(dataMap);
            System.out.println("Calculation Result: " + calculationResult);
        } catch (Exception e) {
            System.err.println("Error: " + e.getMessage());
        }
    }

    private static int complexDataProcessing(Map<String, List<Integer>> dataMap) throws Exception {
        int result = 0;

        for (Map.Entry<String, List<Integer>> entry : dataMap.entrySet()) {
            String key = entry.getKey();
            for (Integer value : entry.getValue()) {
                if (key.startsWith("F")) {
                    result += complexCalculation(value, v -> v * v);
                } else {
                    result -= complexCalculation(value, v -> v + v);
                }
            }
        }

        Optional<Integer> max = dataMap.values().stream()
            .flatMap(Collection::stream)
            .max(Integer::compare);


        final int finalResult = result;

        return max.map(m -> finalResult * m).orElseThrow(() -> new RuntimeException("No maximum value found"));
    }

    private static int complexCalculation(Integer value, Function<Integer, Integer> function) {
        return Optional.ofNullable(value)
            .map(function)
            .orElseThrow(() -> new IllegalArgumentException("Value cannot be null"));
    }
}