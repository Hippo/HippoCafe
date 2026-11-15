import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.jar.JarInputStream;

class VerifyArchives {
    public static void main(String[] args) throws Exception {
        Path directory = Path.of(args[0]);
        verifyJar(directory.resolve("created.jar"), false);
        verifyJar(directory.resolve("rewritten.jar"), true);
    }

    private static void verifyJar(Path path, boolean multiRelease) throws Exception {
        try (var jar = new JarInputStream(Files.newInputStream(path))) {
            var manifest = jar.getManifest();
            if (manifest == null || !"1.0".equals(manifest.getMainAttributes().getValue("Manifest-Version"))) {
                throw new AssertionError("Missing streaming manifest in " + path);
            }
            if (multiRelease != "true".equals(manifest.getMainAttributes().getValue("Multi-Release"))) {
                throw new AssertionError("Incorrect Multi-Release attribute in " + path);
            }
            boolean found = false;
            boolean versioned = false;
            for (var entry = jar.getNextJarEntry(); entry != null; entry = jar.getNextJarEntry()) {
                byte[] bytes = jar.readAllBytes();
                switch (entry.getName()) {
                    case "added.txt" -> found = "jar resource".equals(new String(bytes, StandardCharsets.UTF_8));
                    case "obsolete.txt" -> throw new AssertionError("Removed entry survived in " + path);
                    case "META-INF/versions/17/cafe/fixtures/Features.class" -> versioned = bytes.length > 0;
                }
            }
            if (!found || versioned != multiRelease) {
                throw new AssertionError("Incorrect JAR contents in " + path);
            }
        }
    }
}
