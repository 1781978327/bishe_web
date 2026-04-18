import java.nio.file.Paths;
import java.time.LocalDateTime;

public class HelloJava {
    public static void main(String[] args) {
        System.out.println("Hello from Java!");
        System.out.println("Current time: " + LocalDateTime.now());
        System.out.println("Working dir: " + Paths.get("").toAbsolutePath());
        System.out.println("Java version: " + System.getProperty("java.version"));
        System.out.println("Args count: " + args.length);

        for (int i = 0; i < args.length; i++) {
            System.out.println("arg[" + i + "] = " + args[i]);
        }
    }
}
