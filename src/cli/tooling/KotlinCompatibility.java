import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.jar.JarFile;

// Run with the JDK source launcher. Only inspect the classpath exported by Gradle.
class KotlinCompatibility {
  static Object element(DataInputStream input, Object[] pool) throws IOException {
    int tag = input.readUnsignedByte();
    switch (tag) {
      case 'B': case 'C': case 'D': case 'F': case 'I': case 'J': case 'S': case 'Z': case 's':
        return pool[input.readUnsignedShort()];
      case 'e':
        input.readUnsignedShort();
        input.readUnsignedShort();
        return null;
      case 'c':
        input.readUnsignedShort();
        return null;
      case '@':
        annotation(input, pool, "nested annotation");
        return null;
      case '[':
        int count = input.readUnsignedShort();
        List<Object> values = new ArrayList<>();
        for (int index = 0; index < count; index++) values.add(element(input, pool));
        return values;
      default:
        throw new IOException("Invalid annotation element: " + tag);
    }
  }

  static void annotation(DataInputStream input, Object[] pool, String origin) throws IOException {
    String type = (String) pool[input.readUnsignedShort()];
    int count = input.readUnsignedShort();
    List<?> metadataVersion = null;
    int extraFlags = 0;
    for (int index = 0; index < count; index++) {
      String name = (String) pool[input.readUnsignedShort()];
      Object value = element(input, pool);
      if ("Lkotlin/Metadata;".equals(type) && "mv".equals(name) && value instanceof List<?> version) {
        metadataVersion = version;
      }
      if ("Lkotlin/Metadata;".equals(type) && "xi".equals(name)) extraFlags = (Integer) value;
    }
    if (metadataVersion != null) {
      if (metadataVersion.isEmpty()) throw new IOException("Empty Kotlin metadata version: " + origin);
      // Strict semantics (xi bit 3) disables Kotlin's next-minor compatibility.
      int maximumMinor = (extraFlags & 8) != 0 ? 1 : 2;
      int major = (Integer) metadataVersion.get(0);
      int minor = metadataVersion.size() > 1 ? (Integer) metadataVersion.get(1) : 0;
      if (major == 0 || (major == 1 && minor == 0) || major > 2 || (major == 2 && minor > maximumMinor)) {
        throw new IOException(origin + " uses unsupported Kotlin metadata " + metadataVersion +
            "; fwcd 1.3.13 accepts metadata through 2." + maximumMinor + " for these metadata flags.");
      }
    }
  }

  static void attributes(DataInputStream input, Object[] pool, String origin, boolean inspect) throws IOException {
    int count = input.readUnsignedShort();
    for (int index = 0; index < count; index++) {
      String name = (String) pool[input.readUnsignedShort()];
      long length = Integer.toUnsignedLong(input.readInt());
      if (inspect && ("RuntimeVisibleAnnotations".equals(name) || "RuntimeInvisibleAnnotations".equals(name))) {
        if (length > 1024 * 1024) throw new IOException("Oversized annotation attribute: " + origin);
        byte[] bytes = input.readNBytes((int) length);
        if (bytes.length != length) throw new IOException("Truncated class: " + origin);
        DataInputStream annotations = new DataInputStream(new ByteArrayInputStream(bytes));
        int annotationsCount = annotations.readUnsignedShort();
        for (int item = 0; item < annotationsCount; item++) annotation(annotations, pool, origin);
      } else {
        input.skipNBytes(length);
      }
    }
  }

  static void inspect(InputStream stream, String origin) throws IOException {
    DataInputStream input = new DataInputStream(stream);
    if (input.readInt() != 0xcafebabe) throw new IOException("Invalid class: " + origin);
    input.readUnsignedShort();
    input.readUnsignedShort();
    Object[] pool = new Object[input.readUnsignedShort()];
    for (int index = 1; index < pool.length; index++) {
      int tag = input.readUnsignedByte();
      switch (tag) {
        case 1: pool[index] = input.readUTF(); break;
        case 3: pool[index] = input.readInt(); break;
        case 4: case 9: case 10: case 11: case 12: case 17: case 18: input.skipNBytes(4); break;
        case 5: case 6: input.skipNBytes(8); index++; break;
        case 7: case 8: case 16: case 19: case 20: input.skipNBytes(2); break;
        case 15: input.skipNBytes(3); break;
        default: throw new IOException("Unsupported class constant " + tag + ": " + origin);
      }
    }
    input.skipNBytes(6);
    input.skipNBytes(2L * input.readUnsignedShort());
    for (int section = 0; section < 2; section++) {
      int count = input.readUnsignedShort();
      for (int index = 0; index < count; index++) {
        input.skipNBytes(6);
        attributes(input, pool, origin, false);
      }
    }
    attributes(input, pool, origin, true);
  }

  static void check(Path path) throws IOException {
    if (Files.isDirectory(path)) {
      try (var files = Files.walk(path)) {
        for (Path file : files.filter(item -> item.toString().endsWith(".class")).toList()) {
          try (InputStream input = Files.newInputStream(file)) { inspect(input, file.toString()); }
        }
      }
    } else if (path.toString().endsWith(".jar")) {
      try (JarFile jar = new JarFile(path.toFile())) {
        var entries = jar.entries();
        while (entries.hasMoreElements()) {
          var entry = entries.nextElement();
          if (entry.isDirectory() || !entry.getName().endsWith(".class") || entry.getName().startsWith("META-INF/versions/")) continue;
          try (InputStream input = jar.getInputStream(entry)) { inspect(input, path + "!/" + entry.getName()); }
        }
      }
    } else {
      throw new IOException("Unsupported compile classpath artifact: " + path);
    }
  }

  public static void main(String[] args) {
    try {
      if (Runtime.version().feature() < 21) throw new IOException("The Kotlin adapter requires JDK 21 or later");
      if (args.length != 1) throw new IOException("Expected a classpath inventory file");
      for (String line : Files.readAllLines(Path.of(args[0]))) {
        if (!line.isEmpty()) check(Path.of(line));
      }
    } catch (Exception error) {
      System.err.println("Kotlin compatibility: " + error.getMessage());
      System.exit(1);
    }
  }
}
