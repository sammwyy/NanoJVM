public class GCTest {
  public static void main(String[] args) {
    for (int i = 0; i < 10000; i++) {
      A a = new A();
    }
    System.out.println(1); // to prove execution finishes
  }
}

class A {
  int x;
}
