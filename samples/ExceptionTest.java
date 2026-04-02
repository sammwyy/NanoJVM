import java.lang.*;

public class ExceptionTest {
  public static void main(String[] args) {
    try {
      throwException();
    } catch (Exception e) {
      System.out.println(1);
    }
  }

  static void throwException() {
    throw new RuntimeException();
  }
}
