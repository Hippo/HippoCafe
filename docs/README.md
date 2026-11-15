# HippoCafe documentation

HippoCafe is a C++23 library for inspecting, editing, and writing JVM class files,
JARs, and JMODs. Start with the [project README](../README.md) for building and
read/write examples.

- [Usage](usage.md) covers class editing, frame computation, and archives.
- [Architecture](architecture.md): ownership, module boundaries, handles, revisions, and algorithms.
- [Compatibility](compatibility.md): supported formats, recovery, preservation guarantees, and limitations.
- [Development](development.md): dependencies, validation, examples, fuzzing, benchmarks, and documentation rules.

## Generate the reference

From the repository root, run `doxygen Doxyfile` and open `build/docs/html/index.html`.
The reference includes public contracts and separately grouped implementation details.
Doxygen is needed only for documentation generation; Graphviz and LaTeX are not required.

CMake can generate the same reference in a selected build directory:

```sh
cmake --preset debug -DHIPPOCAFE_BUILD_DOCS=ON
cmake --build build/debug --target hippocafe_docs
```

Open `build/debug/docs/html/index.html`. Missing documentation, malformed commands,
and incomplete parameter documentation fail the documentation build. Generated
files stay in the build tree. Declaration comments define API contracts; these
guides explain workflows and architectural relationships.
