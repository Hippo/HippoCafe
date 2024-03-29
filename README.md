# Hippo Cafe [![Build Status](https://travis-ci.com/Hippo/HippoCafe.svg?branch=master)](https://travis-ci.com/Hippo/HippoCafe)
A Java class file library used to read and write java bytecode written in Scala 3.

This library will attempt to counteract malicious bytecode which will crash other similar libraries.


# Getting started

Add HippoCafe to your project.

```kotlin
repositories {
    maven("https://jitpack.io")
}
```

```kotlin
dependencies {
    implementation("rip.hippo:HippoCafe:2.2.9")
}
```

Or using Scala SBT

```scala
resolvers += "jitpack" at "https://jitpack.io"

libraryDependencies += "rip.hippo" % "HippoCafe" % "2.2.9"
```

# Reading class files

With the `ClassReader` you are able to read class files via a `Array[Byte]` or an `InputStream`.

*It is recommended to use `scala.util.Using` to automatically close the reader.*
```scala
Using(new ClassReader(...)) {
  classReader =>
    // code
}
```

# Manipulating class files

The `ClassFile` gives you access to change every aspect of the class file, if you are familiar with any other Class File library then HippoCafe will be easy to adapt to.

```scala
val classFile: ClassFile = classReader.classFile

val methods: ListBuffer[MethodInfo] = classFile.methods // list of methods
val fields: ListBuffer[FieldInfo] = classfile.fields // list of fields

methods.foreach(method => {
  val instructions: ListBuffer[Instruction] = method.instructions // list of method instructions
})
```

# Writing class files

Once you have a class file that needs to be written you are able to use the `ClassWriter`

*It is recommended to use `scala.util.Using` to automatically close the writer.*

```scala
Using(new ClassWriter(classFile)) {
  classWriter =>
    val bytecode: Array[Byte] = classWriter.write // class files bytecode
}
```

There are optional flags you can pass into the `ClassWriter`
1) calculateMaxes - Calculate the max stack and max locals of each method
2) generateFrames - Generates stack map frames for each method
3) appendConstantPool - Preserves the original classes constant pool, and appends to it if needed.

The options are applied in a builder pattern style fashion, for an example if you wanted a `ClassWriter` to calculateMaxes and generateFrames you would do as such:
```scala
new ClassWriter(classFile).calculateMaxes.generateFrames
```

# Building class files

HippoCafe provides an intuitive class file builder to easily build class files.

```scala
val classFile: Classfile = ClassBuilder(
      SE8,
      "MyClass",
      "java/lang/Object",
      ACC_PUBLIC, ACC_FINAL
    ).method(
      "<init>",
      "()V",
      ACC_PUBLIC
    ).apply(instructions => {
      instructions += SimpleInstruction(ALOAD_0)
      instructions += ReferenceInstruction(INVOKESPECIAL, "java/lang/Object", "<init>", "()V")
      instructions += SimpleInstruction(RETURN)
    }).method(
      "main",
      "([Ljava/lang/String;)V",
      ACC_PUBLIC, ACC_STATIC
    ).apply(instructions => {
      instructions += ReferenceInstruction(GETSTATIC, "java/lang/System", "out", "Ljava/io/PrintStream;")
      instructions += ConstantInstruction(StringConstant("Build from HippoCafe class builder!"))
      instructions += ReferenceInstruction(INVOKEVIRTUAL, "java/io/PrintStream", "println", "(Ljava/lang/String;)V")
      instructions += SimpleInstruction(RETURN)
    }).result
```
