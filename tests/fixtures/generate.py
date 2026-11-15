"""Regenerate the small hand-assembled class files used by the tests."""

from pathlib import Path
from struct import pack


class ConstantPool:
    def __init__(self):
        self.entries = []

    def add(self, tag, body):
        self.entries.append(pack(">B", tag) + body)
        return len(self.entries)

    def utf8(self, text):
        data = text.encode("ascii")
        return self.add(1, pack(">H", len(data)) + data)

    def class_info(self, name):
        return self.add(7, pack(">H", self.utf8(name)))

    def encode(self):
        return pack(">H", 1 + len(self.entries)) + b"".join(self.entries)


def attribute(pool, name, body, declared_length=None):
    length = len(body) if declared_length is None else declared_length
    return pack(">HI", pool.utf8(name), length) + body


def method(pool, version, name, descriptor, instructions, max_stack, max_locals):
    code_header = ">BBH" if version <= (45, 2) else ">HHI"
    code = pack(code_header, max_stack, max_locals, len(instructions))
    code += instructions + pack(">HH", 0, 0)
    member = pack(">HHHH", 0x0009, pool.utf8(name), pool.utf8(descriptor), 1)
    return member + attribute(pool, "Code", code)


def class_file(output, pool, version, name, methods, attributes):
    this_class = pool.class_info(name)
    super_class = pool.class_info("java/lang/Object")
    header = pack(">IHH", 0xCAFEBABE, version[1], version[0])
    envelope = pack(">HHHHHH", 0x0021, this_class, super_class, 0, 0, len(methods))
    tail = pack(">H", len(attributes)) + b"".join(attributes)
    (output / f"{name}.class").write_bytes(header + pool.encode() + envelope + b"".join(methods) + tail)


def main_class(output, name, version, instructions, poison=None):
    pool = ConstantPool()
    main = method(pool, version, "main", "([Ljava/lang/String;)V", instructions, 1, 1)
    attrs = [attribute(pool, "SourceFile", pack(">H", pool.utf8(f"{name}.java")))]
    if poison == "body":
        attrs.append(attribute(pool, "RuntimeVisibleAnnotations", b"\xff\xff\x00\x00"))
    if poison == "length":
        attrs.append(attribute(pool, "RuntimeVisibleAnnotations", b"\x00\x00", 100))
    if poison == "body":
        attrs.append(attribute(pool, "Deprecated", b""))
    class_file(output, pool, version, name, [main], attrs)


def dynamic_constant(output):
    pool = ConstantPool()
    version = (55, 0)
    owner = pool.class_info("java/lang/invoke/ConstantBootstraps")
    name = pool.utf8("nullConstant")
    desc = pool.utf8("(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;Ljava/lang/Class;)Ljava/lang/Object;")
    signature = pool.add(12, pack(">HH", name, desc))
    bootstrap = pool.add(10, pack(">HH", owner, signature))
    handle = pool.add(15, pack(">BH", 6, bootstrap))  # REF_invokeStatic
    name = pool.utf8("value")
    desc = pool.utf8("Ljava/lang/Object;")
    signature = pool.add(12, pack(">HH", name, desc))
    dynamic = pool.add(17, pack(">HH", 0, signature))
    value = method(pool, version, "value", "()Ljava/lang/Object;", bytes((0x12, dynamic, 0xB0)), 1, 0)
    attr = attribute(pool, "BootstrapMethods", pack(">HHH", 1, handle, 0))
    class_file(output, pool, version, "DynamicConstant", [value], [attr])


def large_fixtures(output):
    pool = ConstantPool()
    name = pool.utf8("PrivateMetadata")
    template = pack(">HI", name, 32)
    attributes = [template + bytes((value % 256,)) * 32 for value in range(500)]
    class_file(output, pool, (49, 0), "ManyOpaqueAttributes", [], attributes)

    code = bytearray()
    for i in range(200):
        code.append(0x03)  # iconst_0
        start = len(code)
        code.append(0xAA if i % 2 == 0 else 0xAB)
        code.extend(bytes(-len(code) % 4))
        if i % 2 == 0:
            target = len(code) - start + 12 + 4 * 10
            code.extend(pack(">iii", target, -1, 8) + pack(">i", target) * 10)
        else:
            keys = (-10000, -9, 0, 12, 4567)
            target = len(code) - start + 8 + 8 * len(keys)
            code.extend(pack(">ii", target, len(keys)))
            code.extend(b"".join(pack(">ii", key, target) for key in keys))
    code.append(0xB1)  # return
    main_class(output, "SwitchHeavy", (49, 0), code)


def generate():
    output = Path(__file__).resolve().parent / "generated"
    output.mkdir(exist_ok=True)
    for minor in range(4):
        main_class(output, f"Early45_{minor}", (45, minor), b"\xb1")
    # jsr +4; return; astore_0; ret 0
    main_class(output, "LegacySubroutine", (45, 3), bytes.fromhex("a8 00 04 b1 4b a9 00"))
    main_class(output, "OldPoisonedAttribute", (45, 3), b"\xb1", "body")
    main_class(output, "ModernPoisonedAttribute", (52, 0), b"\xb1", "body")
    main_class(output, "AttributeOverrun", (52, 0), b"\xb1", "length")
    main_class(output, "ReservedOpcode", (52, 0), b"\xca\xb1")
    dynamic_constant(output)
    large_fixtures(output)


if __name__ == "__main__":
    generate()
