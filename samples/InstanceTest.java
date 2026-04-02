class A {
    int x;

    int add(int v) {
        return x + v;
    }
}

public class InstanceTest {
    public static void main(String[] args) {
        A a = new A();
        a.x = 5;
        int result = a.add(3);
        System.out.println(result);
    }
}
