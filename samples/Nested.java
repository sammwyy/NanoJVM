public class Nested {
  public static int add(int a, int b) {
    return a + b;
  }

  public static int addWrap(int a, int b) {
    return add(a, b);
  }

  public static void main(String[] args) {
    int result = addWrap(2, 3);
    System.out.println(result);
  }
}
