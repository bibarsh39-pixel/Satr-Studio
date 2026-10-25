import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;

public class JavaComplex {
    record Item(int id, String name) implements Comparable<Item> {
        public int compareTo(Item other) { return Integer.compare(id, other.id); }
    }
    interface Repository<T> { void add(T item); List<T> all(); }
    static class Store<T> implements Repository<T> {
        private final List<T> items = new ArrayList<>();
        public void add(T item) { items.add(item); }
        public List<T> all() { return items; }
    }
    public static void main(String[] args) throws Exception {
        Repository<Item> store = new Store<>();
        for (int i = 0; i < 100000; ++i) store.add(new Item(i, "item-" + i));
        Collections.shuffle(store.all(), new Random(12345));
        Collections.sort(store.all());
        long sum = store.all().stream().mapToLong(Item::id).sum();
        if (sum != 4999950000L || store.all().get(99999).id() != 99999)
            throw new AssertionError("Collection result mismatch");
        Map<String, Integer> counts = new HashMap<>();
        for (Item item : store.all()) counts.merge(item.id() % 2 == 0 ? "even" : "odd", 1, Integer::sum);
        if (counts.get("even") != 50000) throw new AssertionError("Map mismatch");
        Path file = Path.of("java-roundtrip.txt");
        String message = "Java UTF-8: مرحبا — français";
        Files.writeString(file, message, StandardCharsets.UTF_8);
        if (!Files.readString(file, StandardCharsets.UTF_8).equals(message))
            throw new AssertionError("UTF-8 file roundtrip mismatch");
        Files.delete(file);
        try { Integer.parseInt("not a number"); throw new AssertionError("Expected NumberFormatException"); }
        catch (NumberFormatException expected) { }
        System.out.println("JAVA_COMPLEX:" + sum);
    }
}
