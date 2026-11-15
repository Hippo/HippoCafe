import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.util.*;
import java.util.function.Supplier;

@Retention(RetentionPolicy.RUNTIME)
@Target({ElementType.TYPE_USE, ElementType.TYPE, ElementType.METHOD, ElementType.CONSTRUCTOR, ElementType.PARAMETER, ElementType.LOCAL_VARIABLE, ElementType.FIELD})
@interface Anno {
  String value();
}

@Anno("Class Definition")
public class AnnotationTest<@Anno("Type Parameter") T extends @Anno("Type Parameter Super")ArrayList<@Anno("Type Parameter Type Annotation") String>> implements Cloneable, @Anno("Interface")Supplier<ArrayList<String>> {

  @Anno("Field")
  private final T example;

  @Anno("Constructor")
  public AnnotationTest(@Anno("Constructor Argument") T example) {
    this.example = example;
  }

  @Anno("Method")
  @SuppressWarnings("unchecked")
  public T get(@Anno("Method Argument") int arg) throws @Anno("Throws") Exception {
    @Anno("Local Variable") ArrayList<String> provided = get();
    if (provided == null) {
      throw new @Anno("Exception Type") Exception("Provided string is null");
    }
    return (@Anno("Local Cast Type") T) provided;
  }

  @Override
  public ArrayList<String> get() {
    return new ArrayList<>();
  }

  @Override
  public boolean equals(Object o) {
    if (o instanceof @Anno("Instanceof Type") AnnotationTest)
      return Objects.equals(example, ((@Anno("Generic Cast Type") AnnotationTest<?>) o).example);
    return false;
  }

  @Override
  public @Anno("Return Type") AnnotationTest<@Anno("Generic Parameter") T> clone() throws @Anno("Clone Throws") CloneNotSupportedException {
    return (AnnotationTest<@Anno("Local Generic Cast") T>) super.clone();
  }
}
