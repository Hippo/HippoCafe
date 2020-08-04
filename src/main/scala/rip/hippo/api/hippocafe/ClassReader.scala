/*
 * MIT License
 *
 * Copyright (c) 2020 Hippo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

package rip.hippo.api.hippocafe

import java.io.{ByteArrayInputStream, DataInputStream, InputStream}

import rip.hippo.api.hippocafe.attribute.impl.data.annotation.path.{AnnotationTypePath, AnnotationTypePathKind}
import rip.hippo.api.hippocafe.attribute.impl.data.annotation.path.data.AnnotationTypePathData
import rip.hippo.api.hippocafe.attribute.impl.data.annotation.target.impl.data.LocalVariableTargetData
import rip.hippo.api.hippocafe.attribute.impl.data.annotation.target.{AnnotationTargetInfo, AnnotationTargetInfoKind}
import rip.hippo.api.hippocafe.attribute.impl.data.annotation.target.impl.{CatchTarget, EmptyTarget, FormalParameterTarget, LocalVariableTarget, OffsetTarget, SuperTypeTarget, ThrowsTarget, TypeArgumentTarget, TypeParameterBoundTarget, TypeParameterTarget}
import rip.hippo.api.hippocafe.attribute.impl.data.annotation.value.AnnotationAttributeValue
import rip.hippo.api.hippocafe.attribute.impl.data.annotation.value.impl.{AnnotationAnnotationValue, ArrayValueAnnotationValue, ClassInfoIndexAnnotationValue, ConstantAnnotationValue, EnumConstantAnnotationValue}
import rip.hippo.api.hippocafe.attribute.impl.data.annotation.{AnnotationAttributeData, ElementValuePairAnnotationData, ParameterAnnotationsData, TypeAnnotationData}
import rip.hippo.api.hippocafe.attribute.impl.data.module.{ExportsModuleData, OpensModuleData, ProvidesModuleData, RequiresModuleData}
import rip.hippo.api.hippocafe.attribute.impl.data.{BootstrapMethodsAttributeData, ClassesAttributeData, ExceptionTableAttributeData, LineNumberTableAttributeData, LocalVariableTableAttributeData, LocalVariableTypeTableAttributeData, MethodParametersAttributeData}
import rip.hippo.api.hippocafe.attribute.impl.{AnnotationDefaultAttribute, BootstrapMethodsAttribute, CodeAttribute, ConstantValueAttribute, DeprecatedAttribute, EnclosingMethodAttribute, ExceptionsAttribute, InnerClassesAttribute, LineNumberTableAttribute, LocalVariableTableAttribute, LocalVariableTypeTableAttribute, MethodParametersAttribute, ModuleAttribute, ModuleMainClassAttribute, ModulePackagesAttribute, NestHostAttribute, NestMembersAttribute, RuntimeInvisibleAnnotationsAttribute, RuntimeInvisibleParameterAnnotationsAttribute, RuntimeInvisibleTypeAnnotationsAttribute, RuntimeVisibleAnnotationsAttribute, RuntimeVisibleParameterAnnotationsAttribute, RuntimeVisibleTypeAnnotationsAttribute, SignatureAttribute, SourceDebugExtensionAttribute, SourceFileAttribute, StackMapTableAttribute, SyntheticAttribute, UnknownAttribute}
import rip.hippo.api.hippocafe.attribute.{Attribute, AttributeInfo}
import rip.hippo.api.hippocafe.constantpool.info.impl.{DoubleInfo, DynamicInfo, FloatInfo, IntegerInfo, LongInfo, NameAndTypeInfo, ReferenceInfo, StringInfo, UTF8Info}
import rip.hippo.api.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.api.hippocafe.exception.HippoCafeException
import rip.hippo.api.hippocafe.stackmap.StackMapFrame
import rip.hippo.api.hippocafe.stackmap.impl.{AppendStackMapFrame, ChopStackMapFrame, FullStackMapFrame, SameExtendedStackMapFrame, SameLocalsExtendedStackMapFrame, SameLocalsStackMapFrame, SameStackMapFrame}
import rip.hippo.api.hippocafe.stackmap.verification.VerificationTypeInfo
import rip.hippo.api.hippocafe.stackmap.verification.impl.{DoubleVerificationTypeInfo, FloatVerificationTypeInfo, IntegerVerificationTypeInfo, LongVerificationTypeInfo, NullVerificationTypeInfo, ObjectVerificationTypeInfo, TopVerificationTypeInfo, UninitializedThisVerificationTypeInfo, UninitializedVerificationTypeInfo}
import rip.hippo.api.hippocafe.version.MajorClassFileVersion


/**
 * @author Hippo
 * @version 1.0.0, 8/1/20
 * @since 1.0.0
 */
final class ClassReader(parentInputStream: InputStream) {
  private val inputStream = new DataInputStream(parentInputStream)

  private def u1: Int = inputStream.readUnsignedByte()
  private def u2: Int = inputStream.readUnsignedShort()
  private def u4: Int = inputStream.readInt()

  private val magic = u4
  if (magic != 0xCAFEBABE) throw new HippoCafeException("Magic number is not 0xCAFEBABE")

  private val minorVersion = u2
  private val majorVersion = u2
  private val oak = {
    val version = MajorClassFileVersion.SE1_1.id
    majorVersion < version || (majorVersion == version && minorVersion < 3)
  }


  private val constantPoolSize = u2

  val constantPool: ConstantPool = new ConstantPool

  (1 until constantPoolSize)
    .filterNot(_ => {
      val wide = constantPool.lastWide
      if (wide) u1
      wide
    })
    .foreach(i => {
      import rip.hippo.api.hippocafe.constantpool.ConstantPoolKind._

      val tag = u1
      val constantPoolKind = ConstantPoolKind.fromTag(tag)

      constantPoolKind match {
        case Some(value) =>
          value match {
            case UTF8 => constantPool.insert(i, new UTF8Info(inputStream.readUTF()))
            case CLASS |
                 STRING |
                 METHOD_TYPE |
                 MODULE |
                 PACKAGE => constantPool.insert(i, new StringInfo(u2, value))
            case LONG => constantPool.insert(i, new LongInfo(inputStream.readLong()))
            case DOUBLE => constantPool.insert(i, new DoubleInfo(inputStream.readDouble()))
            case INVOKE_DYNAMIC | DYNAMIC => constantPool.insert(i, new DynamicInfo(value, u2, u2))
            case INTEGER => constantPool.insert(i, new IntegerInfo(u4))
            case FLOAT => constantPool.insert(i, new FloatInfo(inputStream.readFloat()))
            case FIELD_REF |
                 METHOD_REF |
                 INTERFACE_METHOD_REF => constantPool.insert(i, new ReferenceInfo(u2, u2, value))
            case NAME_AND_TYPE => constantPool.insert(i, new NameAndTypeInfo(u2, u2))
          }
        case None => throw new HippoCafeException(s"Could not find constant pool kind for tag $tag")
      }
  })

  constantPool.info.values.foreach(info => info.readCallback(constantPool))


  private val accessMask = u2
  private val className = constantPool.readString(u2)
  private val superName = constantPool.readString(u2)

  val classFile: ClassFile = new ClassFile(MajorClassFileVersion.fromVersion(majorVersion) match {
    case Some(value) => value
    case None => MajorClassFileVersion.SE8
  }, className, superName, accessMask)

  classFile.minorVersion = minorVersion
  classFile.constantPool = Option(constantPool)



  classFile.interfaces.addAll((0 until u2).map(_ => constantPool.readString(u2)))

  classFile.fields.addAll((0 until u2).map(_ => readField))
  classFile.methods.addAll((0 until u2).map(_ => readMethod))
  classFile.attributes.addAll((0 until u2).map(_ => readAttribute()))

  def readField: FieldInfo = {
    val accessMask = u2
    val name = constantPool.readUTF8(u2)
    val descriptor = constantPool.readUTF8(u2)
    val attributesCount = u2
    val fieldInfo = new FieldInfo(name, descriptor, accessMask)
    (0 until attributesCount).foreach(_ => fieldInfo.attributes += readAttribute())
    fieldInfo
  }

  def readMethod: MethodInfo = {
    val accessMask = u2
    val name = constantPool.readUTF8(u2)
    val descriptor = constantPool.readUTF8(u2)
    val attributesCount = u2
    val methodInfo = new MethodInfo(name, descriptor, accessMask)
    (0 until attributesCount).foreach(_ => methodInfo.attributes += readAttribute())
    methodInfo
  }

  def readAttribute(parentStream: DataInputStream = inputStream): AttributeInfo = {

    try {
      val attributeNameIndex = parentStream.readUnsignedShort()
      val attributeLength = parentStream.readInt()

      val buffer = new Array[Byte](attributeLength)
      parentStream.readFully(buffer)

      val bufferStream = new DataInputStream(new ByteArrayInputStream(buffer))

      def u1: Int = bufferStream.readUnsignedByte()
      def u2: Int = bufferStream.readUnsignedShort()
      def u4: Int = bufferStream.readInt()

      def readVerificationTypeInfo: VerificationTypeInfo = {
        u1 match {
          case 0 => new TopVerificationTypeInfo
          case 1 => new IntegerVerificationTypeInfo
          case 2 => new FloatVerificationTypeInfo
          case 3 => new DoubleVerificationTypeInfo
          case 4 => new LongVerificationTypeInfo
          case 5 => new NullVerificationTypeInfo
          case 6 => new UninitializedThisVerificationTypeInfo
          case 7 => new ObjectVerificationTypeInfo(u2)
          case 8 => new UninitializedVerificationTypeInfo(u2)
          case x => throw new HippoCafeException(s"Unable to find type info for tag $x")
        }
      }

      def readAnnotationData: AnnotationAttributeData = {
        val typeIndex = u2
        val numberOfElementPairs = u2
        val elementValuePairs = new Array[ElementValuePairAnnotationData](numberOfElementPairs)
        (0 until numberOfElementPairs).foreach(i => elementValuePairs(i) = ElementValuePairAnnotationData(u2, readAnnotationAttributeValue))
        AnnotationAttributeData(typeIndex, numberOfElementPairs, elementValuePairs)
      }

      def readAnnotationAttributeValue: AnnotationAttributeValue = {
        u1.toChar match {
          case 'B' |
               'C' |
               'D' |
               'F' |
               'I' |
               'J' |
               'S' |
               'Z' |
               's' => ConstantAnnotationValue(u2)
          case 'e' => EnumConstantAnnotationValue(u2, u2)
          case 'c' => ClassInfoIndexAnnotationValue(u2)
          case '@' => AnnotationAnnotationValue(readAnnotationData)
          case '[' =>
            val numberOfValues = u2
            val values = new Array[AnnotationAttributeValue](numberOfValues)
            (0 until numberOfValues).foreach(i => values(i) = readAnnotationAttributeValue)
            ArrayValueAnnotationValue(numberOfValues, values)
        }
      }

      def readTypeAnnotationData: TypeAnnotationData = {
        val targetType = u1
        import rip.hippo.api.hippocafe.attribute.impl.data.annotation.target.AnnotationTargetInfoKind._
        val targetInfo = AnnotationTargetInfoKind.fromId(targetType) match {
          case Some(kind) =>
            kind match {
              case PARAMETER_GENERIC_CLASS | PARAMETER_GENERIC_METHOD => TypeParameterTarget(kind, u1)
              case EXTENDS => SuperTypeTarget(kind, u2)
              case BOUND_GENERIC_CLASS | BOUND_GENERIC_METHOD => TypeParameterBoundTarget(kind, u1, u1)
              case FIELD | RETURN_OR_NEW | RECEIVER => EmptyTarget(kind)
              case FORMAL_PARAMETER => FormalParameterTarget(kind, u1)
              case THROWS => ThrowsTarget(kind, u1)
              case LOCAL_VARIABLE | RESOURCE_VARIABLE =>
                val tableLength = u2
                val table = new Array[LocalVariableTargetData](tableLength)
                (0 until tableLength).foreach(i => table(i) = LocalVariableTargetData(u2, u2, u2))
                LocalVariableTarget(kind, tableLength, table)
              case EXCEPTION_PARAMETER => CatchTarget(kind, u2)
              case INSTANCEOF |
                   NEW |
                   METHOD_REFERENCE_NEW |
                   METHOD_REFERENCE_IDENTIFIER => OffsetTarget(kind, u2)
              case CAST |
                   GENERIC_CONSTRUCTOR |
                   GENERIC_METHOD_INVOCATION |
                   GENERIC_CONSTRUCTOR_NEW |
                   GENERIC_METHOD_IDENTIFIER => TypeArgumentTarget(kind, u2, u1)
            }
          case None => throw new HippoCafeException(s"Could not find target info kind from $targetType")
        }
        val pathLength = u1
        val path = new Array[AnnotationTypePathData](pathLength)
        (0 until pathLength).foreach(i => path(i) = AnnotationTypePathData(AnnotationTypePathKind.fromId(u1) match {
          case Some(value) => value
          case None => throw new HippoCafeException("Could not parse annotation type path kind")
        }, u1))
        val typePath = AnnotationTypePath(pathLength, path)
        val typeIndex = u2
        val numberOfElementValuePairs = u2
        val elementValuePairs = new Array[ElementValuePairAnnotationData](numberOfElementValuePairs)
        (0 until numberOfElementValuePairs).foreach(i => elementValuePairs(i) = ElementValuePairAnnotationData(u2, readAnnotationAttributeValue))
        TypeAnnotationData(targetType, targetInfo, typePath, typeIndex, numberOfElementValuePairs, elementValuePairs)
      }

      val name = constantPool.readUTF8(attributeNameIndex)
      Attribute.fromName(name) match {
        case Some(attribute) =>
          import rip.hippo.api.hippocafe.attribute.Attribute._
          attribute match {
            case CONSTANT_VALUE => ConstantValueAttribute(u2)
            case CODE =>
              val maxStack = if (oak) u1 else u2
              val maxLocals = if (oak) u1 else u2
              val codeLength = if (oak) u2 else u4
              val code = new Array[Byte](codeLength)
              bufferStream.readFully(code)
              val exceptionTableLength = u2
              val exceptionTable = new Array[ExceptionTableAttributeData](exceptionTableLength)
              (0 until exceptionTableLength).foreach(i => exceptionTable(i) = ExceptionTableAttributeData(u2, u2, u2, u2))
              val attributesCount = u2
              val attributes = new Array[AttributeInfo](attributesCount)
              (0 until attributesCount).foreach(i => attributes(i) = readAttribute(bufferStream))
              CodeAttribute(oak, maxStack, maxLocals, codeLength, code, exceptionTableLength, exceptionTable, attributesCount, attributes)
            case STACK_MAP_TABLE =>
              val numberOfEntries = u2
              val entries = new Array[StackMapFrame](numberOfEntries)
              (0 until numberOfEntries).foreach(i => {
                u1 match {
                  case same if same >= 0 && same <= 63 => entries(i) = SameStackMapFrame(same)
                  case sameLocal if sameLocal >= 64 && sameLocal <= 127 => entries(i) = SameLocalsStackMapFrame(sameLocal, Array[VerificationTypeInfo](readVerificationTypeInfo))
                  case 247 => entries(i) = SameLocalsExtendedStackMapFrame(u2, Array[VerificationTypeInfo](readVerificationTypeInfo))
                  case chop if chop >= 248 && chop <= 250 => entries(i) = ChopStackMapFrame(chop, u2)
                  case 251 => entries(i) = SameExtendedStackMapFrame(u2)
                  case append if append >= 252 && append <= 254 =>
                    val length = append - 251
                    val offsetDelta = u2
                    val stack = new Array[VerificationTypeInfo](length)
                    (0 until length).foreach(k => stack(k) = readVerificationTypeInfo)
                    entries(i) = AppendStackMapFrame(append, offsetDelta, stack)
                  case 255 =>
                    val offsetDelta = u2
                    val numberOfLocals = u2
                    val locals = new Array[VerificationTypeInfo](numberOfLocals)
                    (0 until numberOfLocals).foreach(k => locals(k) = readVerificationTypeInfo)
                    val numberOfStackItems = u2
                    val stack = new Array[VerificationTypeInfo](numberOfStackItems)
                    (0 until numberOfStackItems).foreach(k => stack(k) = readVerificationTypeInfo)
                    entries(i) = FullStackMapFrame(offsetDelta, numberOfLocals, locals, numberOfStackItems, stack)
                }
              })
              StackMapTableAttribute(numberOfEntries, entries)
            case EXCEPTIONS =>
              val numberOfExceptions = u2
              val exceptionIndexTable = new Array[Int](numberOfExceptions)
              (0 until numberOfExceptions).foreach(i => exceptionIndexTable(i) = u2)
              ExceptionsAttribute(numberOfExceptions, exceptionIndexTable)
            case INNER_CLASSES =>
              val numberOfClasses = u2
              val classes = new Array[ClassesAttributeData](numberOfClasses)
              (0 until numberOfClasses).foreach(i => classes(i) = ClassesAttributeData(u2, u2, u2, u2))
              InnerClassesAttribute(numberOfClasses, classes)
            case ENCLOSING_METHOD => EnclosingMethodAttribute(u2, u2)
            case SYNTHETIC => SyntheticAttribute()
            case SIGNATURE => SignatureAttribute(u2)
            case SOURCE_FILE => SourceFileAttribute(u2)
            case SOURCE_DEBUG_EXTENSION => SourceDebugExtensionAttribute(buffer)
            case LINE_NUMBER_TABLE =>
              val lineNumberTableLength = u2
              val lineNumberTable = new Array[LineNumberTableAttributeData](lineNumberTableLength)
              (0 until lineNumberTableLength).foreach(i => lineNumberTable(i) = LineNumberTableAttributeData(u2, u2))
              LineNumberTableAttribute(lineNumberTableLength, lineNumberTable)
            case LOCAL_VARIABLE_TABLE =>
              val localVariableTableLength = u2
              val localVariableTable = new Array[LocalVariableTableAttributeData](localVariableTableLength)
              (0 until localVariableTableLength).foreach(i => localVariableTable(i) = LocalVariableTableAttributeData(u2, u2, u2, u2, u2))
              LocalVariableTableAttribute(localVariableTableLength, localVariableTable)
            case LOCAL_VARIABLE_TYPE_TABLE =>
              val localVariableTypeTableLength = u2
              val localVariableTypeTable = new Array[LocalVariableTypeTableAttributeData](localVariableTypeTableLength)
              (0 until localVariableTypeTableLength).foreach(i => localVariableTypeTable(i) = LocalVariableTypeTableAttributeData(u2, u2, u2, u2, u2))
              LocalVariableTypeTableAttribute(localVariableTypeTableLength, localVariableTypeTable)
            case DEPRECATED => DeprecatedAttribute()
            case RUNTIME_VISIBLE_ANNOTATIONS =>
              val numberOfAnnotations = u2
              val annotations = new Array[AnnotationAttributeData](numberOfAnnotations)
              (0 until numberOfAnnotations).foreach(i => annotations(i) = readAnnotationData)
              RuntimeVisibleAnnotationsAttribute(numberOfAnnotations, annotations)
            case RUNTIME_INVISIBLE_ANNOTATIONS =>
              val numberOfAnnotations = u2
              val annotations = new Array[AnnotationAttributeData](numberOfAnnotations)
              (0 until numberOfAnnotations).foreach(i => annotations(i) = readAnnotationData)
              RuntimeInvisibleAnnotationsAttribute(numberOfAnnotations, annotations)
            case RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS =>
              val numberOfParameters = u1
              val parameterAnnotations = new Array[ParameterAnnotationsData](numberOfParameters)
              (0 until numberOfParameters).foreach(i => {
                val numberOfAnnotations = u2
                val annotations = new Array[AnnotationAttributeData](numberOfAnnotations)
                (0 until numberOfAnnotations).foreach(k => annotations(k) = readAnnotationData)
                parameterAnnotations(i) = ParameterAnnotationsData(numberOfAnnotations, annotations)
              })
              RuntimeVisibleParameterAnnotationsAttribute(numberOfParameters, parameterAnnotations)
            case RUNTIME_INVISIBLE_PARAMETER_ANNOTATIONS =>
              val numberOfParameters = u1
              val parameterAnnotations = new Array[ParameterAnnotationsData](numberOfParameters)
              (0 until numberOfParameters).foreach(i => {
                val numberOfAnnotations = u2
                val annotations = new Array[AnnotationAttributeData](numberOfAnnotations)
                (0 until numberOfAnnotations).foreach(k => annotations(k) = readAnnotationData)
                parameterAnnotations(i) = ParameterAnnotationsData(numberOfAnnotations, annotations)
              })
              RuntimeInvisibleParameterAnnotationsAttribute(numberOfParameters, parameterAnnotations)
            case RUNTIME_VISIBLE_TYPE_ANNOTATIONS =>
              val numberOfAnnotations = u2
              val annotations = new Array[TypeAnnotationData](numberOfAnnotations)
              (0 until numberOfAnnotations).foreach(i => annotations(i) = readTypeAnnotationData)
              RuntimeVisibleTypeAnnotationsAttribute(numberOfAnnotations, annotations)
            case RUNTIME_INVISIBLE_TYPE_ANNOTATIONS =>
              val numberOfAnnotations = u2
              val annotations = new Array[TypeAnnotationData](numberOfAnnotations)
              (0 until numberOfAnnotations).foreach(i => annotations(i) = readTypeAnnotationData)
              RuntimeInvisibleTypeAnnotationsAttribute(numberOfAnnotations, annotations)
            case ANNOTATION_DEFAULT => AnnotationDefaultAttribute(readAnnotationAttributeValue)
            case BOOTSTRAP_METHODS =>
              val numberOfBootstrapMethods = u2
              val bootstrapMethods = new Array[BootstrapMethodsAttributeData](numberOfBootstrapMethods)
              (0 until numberOfBootstrapMethods).foreach(i => {
                val bootstrapMethodRef = u2
                val numberOfBootstrapArguments = u2
                val bootstrapArguments = new Array[Int](numberOfBootstrapArguments)
                (0 until numberOfBootstrapArguments).foreach(i => bootstrapArguments(i) = u2)
                bootstrapMethods(i) = BootstrapMethodsAttributeData(bootstrapMethodRef, numberOfBootstrapArguments, bootstrapArguments)
              })
              BootstrapMethodsAttribute(numberOfBootstrapMethods, bootstrapMethods)
            case METHOD_PARAMETERS =>
              val parametersCount = u1
              val parameters = new Array[MethodParametersAttributeData](parametersCount)
              (0 until parametersCount).foreach(i => parameters(i) = MethodParametersAttributeData(u2, u2))
              MethodParametersAttribute(parametersCount, parameters)
            case MODULE =>
              val moduleNameIndex = u2
              val moduleFlags = u2
              val moduleVersionIndex = u2

              val requiresCount = u2
              val requires = new Array[RequiresModuleData](requiresCount)
              (0 until requiresCount).foreach(i => requires(i) = RequiresModuleData(u2, u2, u2))

              val exportsCount = u2
              val exports = new Array[ExportsModuleData](exportsCount)
              (0 until exportsCount).foreach(i => {
                val exportsIndex = u2
                val exportsFlags = u2
                val exportsToCount = u2
                val exportsToIndex = new Array[Int](exportsToCount)
                (0 until exportsToCount).foreach(k => exportsToIndex(k) = u2)
                exports(i) = ExportsModuleData(exportsIndex, exportsFlags, exportsToCount, exportsToIndex)
              })

              val opensCount = u2
              val opens = new Array[OpensModuleData](opensCount)
              (0 until opensCount).foreach(i => {
                val opensIndex = u2
                val opensFlags = u2
                val opensToCount = u2
                val opensToIndex = new Array[Int](opensToCount)
                (0 until opensToCount).foreach(k => opensToIndex(k) = u2)
                opens(i) = OpensModuleData(opensIndex, opensFlags, opensToCount, opensToIndex)
              })

              val usesCount = u2
              val usesIndex = new Array[Int](usesCount)
              (0 until usesCount).foreach(i => usesIndex(i) = u2)

              val providesCount = u2
              val provides = new Array[ProvidesModuleData](providesCount)
              (0 until providesCount).foreach(i => {
                val providesIndex = u2
                val providesWithCount = u2
                val providesWithIndex = new Array[Int](providesWithCount)
                (0 until providesWithCount).foreach(k => providesWithIndex(k) = u2)
                provides(i) = ProvidesModuleData(providesIndex, providesWithCount, providesWithIndex)
              })
              ModuleAttribute(moduleNameIndex, moduleFlags, moduleVersionIndex, requiresCount, requires, exportsCount, exports, opensCount, opens, usesCount, usesIndex, providesCount, provides)
            case MODULE_PACKAGES =>
              val packageCount = u2
              val packageIndex = new Array[Int](packageCount)
              (0 until packageCount).foreach(i => packageIndex(i) = u2)
              ModulePackagesAttribute(packageCount, packageIndex)
            case MODULE_MAIN_CLASS => ModuleMainClassAttribute(u2)
            case NEST_HOST => NestHostAttribute(u2)
            case NEST_MEMBERS =>
              val numberOfClasses = u2
              val classes = new Array[Int](numberOfClasses)
              (0 until numberOfClasses).foreach(i => classes(i) = u2)
              NestMembersAttribute(numberOfClasses, classes)
            case _ => UnknownAttribute(name, buffer)
          }
        case None => UnknownAttribute(name, buffer)
      }
    } finally if (parentStream != null && parentStream != inputStream) parentStream.close()
  }
}