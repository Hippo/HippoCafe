# hippocafe

A java bytecode manipulation library written in C++.

## Building
```shell
mkdir build
cd build
cmake ..
cmake --build . --config Release
cmake --install . --config Release
```

## Example Usage
```cpp
#include <hippo/cafe.hpp>

using namespace cafe;

// The source of the class file, can either be a std::vector<int8_t> or std::istream.
std::ifstream source("data/HelloWorld.class", std::ios::binary);

class_reader reader;
result<class_file> read_result = reader.read(source);
if (!read_result) {
  std::cerr << read_result.err().message() << std::endl;
  return;
}
class_file& file = read_result.value();

for (auto& method : file.methods) {
  for (auto& insn : method.code) {
    if (auto* push = std::get_if<push_insn>(&insn)) {
      if (push->operand == value{"Hello World"}) {
        push->operand = "Modified";
      }
    }
  }
}

class_writer writer;
std::vector<int8_t> data = writer.write(file);
std::ofstream os("HelloWorld.class", std::ios::binary);
os.write(reinterpret_cast<const char*>(data.data()), data.size());
```

## Class Writer Flags
hippocafe's class writer can be configured to automatically compute the method maxes (max stack and max locals) and/or stack map frames.
In order to compute maxes, simply pass `cafe::compute_maxes` to your `cafe::class_writer`
```cpp
cafe::class_writer writer(cafe::compute_maxes);
```
To compute frames you must create a `cafe::class_tree`, then pass it to the class writer as such.
```cpp
cafe::class_tree tree(cafe::load_rt);
cafe::class_writer writer(tree);
```
For more accurate frame generation, you can add class information to the class tree.
```cpp
tree.put("my/ThisClass", "my/SuperClass", {"my/Interface1", "my/Interface2"});
// or
cafe::class_file file = ...;
tree.put(file);
```