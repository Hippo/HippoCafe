/*
 * MIT License
 *
 * Copyright (c) 2021 Hippo
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

package rip.hippo.hippocafe

import rip.hippo.hippocafe.access.AccessFlag

import java.io.{ByteArrayInputStream, DataInputStream, InputStream}
import rip.hippo.hippocafe.constantpool.ConstantPoolKind._
import rip.hippo.hippocafe.attribute.impl.{AnnotationDefaultAttribute, BootstrapMethodsAttribute, CodeAttribute, ConstantValueAttribute, DeprecatedAttribute, EnclosingMethodAttribute, ExceptionsAttribute, InnerClassesAttribute, LineNumberTableAttribute, LocalVariableTableAttribute, LocalVariableTypeTableAttribute, MethodParametersAttribute, ModuleAttribute, ModuleMainClassAttribute, ModulePackagesAttribute, NestHostAttribute, NestMembersAttribute, RecordAttribute, RuntimeInvisibleAnnotationsAttribute, RuntimeInvisibleParameterAnnotationsAttribute, RuntimeInvisibleTypeAnnotationsAttribute, RuntimeVisibleAnnotationsAttribute, RuntimeVisibleParameterAnnotationsAttribute, RuntimeVisibleTypeAnnotationsAttribute, SignatureAttribute, SourceDebugExtensionAttribute, SourceFileAttribute, StackMapTableAttribute, SyntheticAttribute, UnknownAttribute}
import rip.hippo.hippocafe.attribute.impl.data.{BootstrapMethodsAttributeData, ClassesAttributeData, ExceptionTableAttributeData, LineNumberTableAttributeData, LocalVariableTableAttributeData, LocalVariableTypeTableAttributeData, MethodParametersAttributeData, RecordComponentInfo, annotation}
import rip.hippo.hippocafe.attribute.{Attribute, AttributeInfo}
import rip.hippo.hippocafe.attribute.impl.data.annotation.path.{AnnotationTypePath, AnnotationTypePathKind, data}
import rip.hippo.hippocafe.attribute.impl.data.annotation.path.data.AnnotationTypePathData
import rip.hippo.hippocafe.attribute.impl.data.annotation.target.AnnotationTargetTypeKind
import rip.hippo.hippocafe.attribute.impl.data.annotation.target.impl.{CatchTarget, EmptyTarget, FormalParameterTarget, LocalVariableTarget, OffsetTarget, SuperTypeTarget, ThrowsTarget, TypeArgumentTarget, TypeParameterBoundTarget, TypeParameterTarget}
import rip.hippo.hippocafe.attribute.impl.data.annotation.target.impl.data.LocalVariableTargetData
import rip.hippo.hippocafe.attribute.impl.data.annotation.value.AnnotationAttributeValue
import rip.hippo.hippocafe.attribute.impl.data.annotation.value.impl.{AnnotationAnnotationValue, ArrayValueAnnotationValue, ClassInfoIndexAnnotationValue, ConstantAnnotationValue, EnumConstantAnnotationValue}
import rip.hippo.hippocafe.attribute.impl.data.annotation.{AnnotationAttributeData, ElementValuePairAnnotationData, ParameterAnnotationsData, TypeAnnotationData}
import rip.hippo.hippocafe.attribute.impl.data.module.{ExportsModuleData, OpensModuleData, ProvidesModuleData, RequiresModuleData}
import rip.hippo.hippocafe.constantpool.{ConstantPool, ConstantPoolKind}
import rip.hippo.hippocafe.constantpool.info.impl.{DoubleInfo, DynamicInfo, FloatInfo, IntegerInfo, LongInfo, MethodHandleInfo, NameAndTypeInfo, ReferenceInfo, StringInfo, UTF8Info}
import rip.hippo.hippocafe.constantpool.info.impl.data.ReferenceKind
import rip.hippo.hippocafe.disassembler.instruction.CodeDisassembler
import rip.hippo.hippocafe.disassembler.instruction.constant.Constant
import rip.hippo.hippocafe.exception.HippoCafeException
import rip.hippo.hippocafe.stackmap.StackMapFrame
import rip.hippo.hippocafe.stackmap.impl.{AppendStackMapFrame, ChopStackMapFrame, FullStackMapFrame, SameExtendedStackMapFrame, SameLocalsExtendedStackMapFrame, SameLocalsStackMapFrame, SameStackMapFrame}
import rip.hippo.hippocafe.stackmap.verification.VerificationTypeInfo
import rip.hippo.hippocafe.stackmap.verification.impl.{DoubleVerificationTypeInfo, FloatVerificationTypeInfo, IntegerVerificationTypeInfo, LongVerificationTypeInfo, NullVerificationTypeInfo, ObjectVerificationTypeInfo, TopVerificationTypeInfo, UninitializedThisVerificationTypeInfo, UninitializedVerificationTypeInfo}
import rip.hippo.hippocafe.version.MajorClassFileVersion

import scala.collection.mutable.ArrayBuffer


/**
 * @author Hippo
 * @version 1.1.0, 8/1/20
 * @since 1.0.0
 */
final class ClassReader(parentInputStream: InputStream, lowLevel: Boolean = false) extends AutoCloseable {

  def this(bytecode: Array[Byte], lowLevel: Boolean) {
    this(new ByteArrayInputStream(bytecode), lowLevel)
  }

  def this(bytecode: Array[Byte]) {
    this(bytecode, false)
  }

  private val inputStream = new DataInputStream(parentInputStream)
  private def u1: Int = inputStream.readUnsignedByte()
  private def u2: Int = inputStream.readUnsignedShort()
  private def u4: Int = inputStream.readInt()

  u4

  private val minorVersion = u2
  private val majorVersion = u2
  private val oak = {
    val version = MajorClassFileVersion.JDK1_1.id
    majorVersion < version || (majorVersion == version && minorVersion < 3)
  }


  private val constantPoolSize = u2

  val constantPool: ConstantPool = new ConstantPool


  var i = 1
  while (i < constantPoolSize) {
    val tag = u1
    val constantPoolKind = ConstantPoolKind.fromTag(tag)

    constantPoolKind match {
      case Some(value) =>
        value match {
          case UTF8 => constantPool.insert(i, UTF8Info(inputStream.readUTF()))
          case CLASS |
               STRING |
               METHOD_TYPE |
               MODULE |
               PACKAGE => constantPool.insert(i, StringInfo(u2, value))
          case LONG =>
            constantPool.insert(i, LongInfo(inputStream.readLong()))
            i += 1
          case DOUBLE =>
            constantPool.insert(i, DoubleInfo(inputStream.readDouble()))
            i += 1
          case INVOKE_DYNAMIC | DYNAMIC => constantPool.insert(i, DynamicInfo(value, u2, u2))
          case INTEGER => constantPool.insert(i, IntegerInfo(u4))
          case FLOAT => constantPool.insert(i, FloatInfo(inputStream.readFloat()))
          case FIELD_REF |
               METHOD_REF |
               INTERFACE_METHOD_REF => constantPool.insert(i, ReferenceInfo(u2, u2, value))
          case NAME_AND_TYPE => constantPool.insert(i, NameAndTypeInfo(u2, u2))
          case METHOD_HANDLE => constantPool.insert(i, MethodHandleInfo(ReferenceKind.valueOf(u1), u2))
        }
      case None => throw new HippoCafeException(s"Could not find constant pool kind for tag $tag")
    }
    i += 1
  }

  constantPool.info.values.foreach(info => info.readCallback(constantPool))


  private val accessMask = u2
  private val className = constantPool.readString(u2)
  private val superName = constantPool.readString(u2)

  val classFile: ClassFile = new ClassFile(MajorClassFileVersion.fromVersion(majorVersion) match {
    case Some(value) => value
    case None => MajorClassFileVersion.SE11
  }, className, superName, accessMask)

  classFile.lowLevel = lowLevel

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
          case 7 => new ObjectVerificationTypeInfo(constantPool.readString(u2))
          case 8 => new UninitializedVerificationTypeInfo(u2)
          case x => throw new HippoCafeException(s"Unable to find type info for tag $x")
        }
      }

      def readAnnotationData: AnnotationAttributeData = {
        val typeIndex = u2
        val numberOfElementPairs = u2
        val elementValuePairs = new Array[ElementValuePairAnnotationData](numberOfElementPairs)
        (0 until numberOfElementPairs).foreach(i => elementValuePairs(i) = ElementValuePairAnnotationData(constantPool.readUTF8(u2), readAnnotationAttributeValue))
        AnnotationAttributeData(constantPool.readUTF8(typeIndex), elementValuePairs)
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
               's' => ConstantAnnotationValue(Constant.fromInfo(constantPool.info(u2)))
          case 'e' => EnumConstantAnnotationValue(constantPool.readUTF8(u2), constantPool.readUTF8(u2))
          case 'c' => ClassInfoIndexAnnotationValue(constantPool.readUTF8(u2))
          case '@' => AnnotationAnnotationValue(readAnnotationData)
          case '[' =>
            val numberOfValues = u2
            val values = new Array[AnnotationAttributeValue](numberOfValues)
            (0 until numberOfValues).foreach(i => values(i) = readAnnotationAttributeValue)
            ArrayValueAnnotationValue(values)
        }
      }

      def readTypeAnnotationData: TypeAnnotationData = {
        val targetType = u1
        import rip.hippo.hippocafe.attribute.impl.data.annotation.target.AnnotationTargetTypeKind._
        val targetInfo = AnnotationTargetTypeKind.fromId(targetType) match {
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
        (0 until pathLength).foreach(i => path(i) = data.AnnotationTypePathData(AnnotationTypePathKind.fromId(u1) match {
          case Some(value) => value
          case None => throw new HippoCafeException("Could not parse annotation type path kind")
        }, u1))
        val typePath = AnnotationTypePath(path)
        val typeIndex = u2
        val numberOfElementValuePairs = u2
        val elementValuePairs = new Array[ElementValuePairAnnotationData](numberOfElementValuePairs)
        (0 until numberOfElementValuePairs).foreach(i => elementValuePairs(i) = ElementValuePairAnnotationData(constantPool.readUTF8(u2), readAnnotationAttributeValue))
        annotation.TypeAnnotationData(targetType, targetInfo, typePath, typeIndex, elementValuePairs)
      }

      val name = constantPool.readUTF8(attributeNameIndex)
      Attribute.fromName(name) match {
        case Some(attribute) =>
          import Attribute._
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
              (0 until exceptionTableLength).foreach(i => exceptionTable(i) = ExceptionTableAttributeData(u2, u2, u2, constantPool.readString(u2)))
              val attributesCount = u2
              val attributes = new Array[AttributeInfo](attributesCount)
              (0 until attributesCount).foreach(i => attributes(i) = readAttribute(bufferStream))
              CodeAttribute(oak, maxStack, maxLocals, code, exceptionTableLength, exceptionTable, attributes)
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
              StackMapTableAttribute(entries)
            case EXCEPTIONS =>
              val numberOfExceptions = u2
              val exceptionIndexTable = new Array[Int](numberOfExceptions)
              (0 until numberOfExceptions).foreach(i => exceptionIndexTable(i) = u2)
              ExceptionsAttribute(exceptionIndexTable)
            case INNER_CLASSES =>
              val numberOfClasses = u2
              val classes = new Array[ClassesAttributeData](numberOfClasses)
              (0 until numberOfClasses).foreach(i => {
                val innerClassInfoIndex = u2
                val outerClassInfoIndex = u2
                val innerNameIndex = u2
                classes(i) = ClassesAttributeData(
                  constantPool.readString(innerClassInfoIndex),
                  if (outerClassInfoIndex == 0) Option.empty else Option(constantPool.readString(outerClassInfoIndex)),
                  if (innerNameIndex == 0) Option.empty else Option(constantPool.readUTF8(innerNameIndex)),
                  u2)
              })
              InnerClassesAttribute(classes)
            case ENCLOSING_METHOD => EnclosingMethodAttribute(u2, u2)
            case SYNTHETIC => SyntheticAttribute()
            case SIGNATURE => SignatureAttribute(u2)
            case SOURCE_FILE => SourceFileAttribute(constantPool.readUTF8(u2))
            case SOURCE_DEBUG_EXTENSION => SourceDebugExtensionAttribute(buffer)
            case LINE_NUMBER_TABLE =>
              val lineNumberTableLength = u2
              val lineNumberTable = new Array[LineNumberTableAttributeData](lineNumberTableLength)
              (0 until lineNumberTableLength).foreach(i => lineNumberTable(i) = LineNumberTableAttributeData(u2, u2))
              LineNumberTableAttribute(lineNumberTable)
            case LOCAL_VARIABLE_TABLE =>
              val localVariableTableLength = u2
              val localVariableTable = new Array[LocalVariableTableAttributeData](localVariableTableLength)
              (0 until localVariableTableLength).foreach(i => localVariableTable(i) = LocalVariableTableAttributeData(u2, u2, constantPool.readUTF8(u2), constantPool.readUTF8(u2), u2))
              LocalVariableTableAttribute(localVariableTable)
            case LOCAL_VARIABLE_TYPE_TABLE =>
              val localVariableTypeTableLength = u2
              val localVariableTypeTable = new Array[LocalVariableTypeTableAttributeData](localVariableTypeTableLength)
              (0 until localVariableTypeTableLength).foreach(i => localVariableTypeTable(i) = LocalVariableTypeTableAttributeData(u2, u2, constantPool.readUTF8(u2), constantPool.readUTF8(u2), u2))
              LocalVariableTypeTableAttribute(localVariableTypeTable)
            case DEPRECATED => DeprecatedAttribute()
            case RUNTIME_VISIBLE_ANNOTATIONS =>
              val numberOfAnnotations = u2
              val annotations = new Array[AnnotationAttributeData](numberOfAnnotations)
              (0 until numberOfAnnotations).foreach(i => annotations(i) = readAnnotationData)
              RuntimeVisibleAnnotationsAttribute(annotations)
            case RUNTIME_INVISIBLE_ANNOTATIONS =>
              val numberOfAnnotations = u2
              val annotations = new Array[AnnotationAttributeData](numberOfAnnotations)
              (0 until numberOfAnnotations).foreach(i => annotations(i) = readAnnotationData)
              RuntimeInvisibleAnnotationsAttribute(annotations)
            case RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS =>
              val numberOfParameters = u1
              val parameterAnnotations = new Array[ParameterAnnotationsData](numberOfParameters)
              (0 until numberOfParameters).foreach(i => {
                val numberOfAnnotations = u2
                val annotations = new Array[AnnotationAttributeData](numberOfAnnotations)
                (0 until numberOfAnnotations).foreach(k => annotations(k) = readAnnotationData)
                parameterAnnotations(i) = ParameterAnnotationsData(annotations)
              })
              RuntimeVisibleParameterAnnotationsAttribute(parameterAnnotations)
            case RUNTIME_INVISIBLE_PARAMETER_ANNOTATIONS =>
              val numberOfParameters = u1
              val parameterAnnotations = new Array[ParameterAnnotationsData](numberOfParameters)
              (0 until numberOfParameters).foreach(i => {
                val numberOfAnnotations = u2
                val annotations = new Array[AnnotationAttributeData](numberOfAnnotations)
                (0 until numberOfAnnotations).foreach(k => annotations(k) = readAnnotationData)
                parameterAnnotations(i) = ParameterAnnotationsData(annotations)
              })
              RuntimeInvisibleParameterAnnotationsAttribute(parameterAnnotations)
            case RUNTIME_VISIBLE_TYPE_ANNOTATIONS =>
              val numberOfAnnotations = u2
              val annotations = new Array[TypeAnnotationData](numberOfAnnotations)
              (0 until numberOfAnnotations).foreach(i => annotations(i) = readTypeAnnotationData)
              RuntimeVisibleTypeAnnotationsAttribute(annotations)
            case RUNTIME_INVISIBLE_TYPE_ANNOTATIONS =>
              val numberOfAnnotations = u2
              val annotations = new Array[TypeAnnotationData](numberOfAnnotations)
              (0 until numberOfAnnotations).foreach(i => annotations(i) = readTypeAnnotationData)
              RuntimeInvisibleTypeAnnotationsAttribute(annotations)
            case ANNOTATION_DEFAULT => AnnotationDefaultAttribute(readAnnotationAttributeValue)
            case BOOTSTRAP_METHODS =>
              val numberOfBootstrapMethods = u2
              val bootstrapMethods = new Array[BootstrapMethodsAttributeData](numberOfBootstrapMethods)
              (0 until numberOfBootstrapMethods).foreach(i => {
                val bootstrapMethodRef = u2
                val numberOfBootstrapArguments = u2
                val bootstrapArguments = new Array[Int](numberOfBootstrapArguments)
                (0 until numberOfBootstrapArguments).foreach(i => bootstrapArguments(i) = u2)
                bootstrapMethods(i) = BootstrapMethodsAttributeData(bootstrapMethodRef, bootstrapArguments)
              })
              BootstrapMethodsAttribute(bootstrapMethods)
            case METHOD_PARAMETERS =>
              val parametersCount = u1
              val parameters = new Array[MethodParametersAttributeData](parametersCount)
              (0 until parametersCount).foreach(i => parameters(i) = MethodParametersAttributeData(u2, u2))
              MethodParametersAttribute(parameters)
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
                exports(i) = ExportsModuleData(exportsIndex, exportsFlags, exportsToIndex)
              })

              val opensCount = u2
              val opens = new Array[OpensModuleData](opensCount)
              (0 until opensCount).foreach(i => {
                val opensIndex = u2
                val opensFlags = u2
                val opensToCount = u2
                val opensToIndex = new Array[Int](opensToCount)
                (0 until opensToCount).foreach(k => opensToIndex(k) = u2)
                opens(i) = OpensModuleData(opensIndex, opensFlags, opensToIndex)
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
                provides(i) = ProvidesModuleData(providesIndex, providesWithIndex)
              })
              ModuleAttribute(moduleNameIndex, moduleFlags, moduleVersionIndex, requires, exports, opens, usesIndex, provides)
            case MODULE_PACKAGES =>
              val packageCount = u2
              val packageIndex = new Array[Int](packageCount)
              (0 until packageCount).foreach(i => packageIndex(i) = u2)
              ModulePackagesAttribute(packageIndex)
            case MODULE_MAIN_CLASS => ModuleMainClassAttribute(u2)
            case NEST_HOST => NestHostAttribute(u2)
            case NEST_MEMBERS =>
              val numberOfClasses = u2
              val classes = new Array[Int](numberOfClasses)
              (0 until numberOfClasses).foreach(i => classes(i) = u2)
              NestMembersAttribute(classes)
            case RECORD =>
              val componentsCount = u2
              val components = new Array[RecordComponentInfo](componentsCount)
              (0 until componentsCount).foreach(i => {
                val name = constantPool.readUTF8(u2)
                val descriptor = constantPool.readUTF8(u2)
                val attributesCount = u2
                val attributes = new Array[AttributeInfo](attributesCount)
                (0 until attributesCount).foreach(i => attributes(i) = readAttribute(bufferStream))
                components(i) = RecordComponentInfo(name, descriptor, attributes)
              })
              RecordAttribute(components)
            case _ => UnknownAttribute(name, buffer)
          }
        case None => UnknownAttribute(name, buffer)
      }
    } finally if (parentStream != null && parentStream != inputStream) parentStream.close()
  }

  if (!lowLevel) classFile.methods.foreach(methodInfo => CodeDisassembler.disassemble(methodInfo, constantPool))

  override def close(): Unit = inputStream.close()
}