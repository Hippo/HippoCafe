package rip.hippo.hippocafe.io.reader.impl

import rip.hippo.hippocafe.attribute.Attribute.{ANNOTATION_DEFAULT, BOOTSTRAP_METHODS, CODE, CONSTANT_VALUE, DEPRECATED, ENCLOSING_METHOD, EXCEPTIONS, INNER_CLASSES, LINE_NUMBER_TABLE, LOCAL_VARIABLE_TABLE, LOCAL_VARIABLE_TYPE_TABLE, METHOD_PARAMETERS, MODULE, MODULE_MAIN_CLASS, MODULE_PACKAGES, NEST_HOST, NEST_MEMBERS, RECORD, RUNTIME_INVISIBLE_ANNOTATIONS, RUNTIME_INVISIBLE_PARAMETER_ANNOTATIONS, RUNTIME_INVISIBLE_TYPE_ANNOTATIONS, RUNTIME_VISIBLE_ANNOTATIONS, RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS, RUNTIME_VISIBLE_TYPE_ANNOTATIONS, SIGNATURE, SOURCE_DEBUG_EXTENSION, SOURCE_FILE, STACK_MAP_TABLE, SYNTHETIC}
import rip.hippo.hippocafe.attribute.impl.data.annotation.path.data.AnnotationTypePathData
import rip.hippo.hippocafe.attribute.impl.data.annotation.path.{AnnotationTypePath, AnnotationTypePathKind, data}
import rip.hippo.hippocafe.attribute.impl.data.annotation.target.AnnotationTargetTypeKind
import rip.hippo.hippocafe.attribute.impl.data.annotation.target.impl.data.LocalVariableTargetData
import rip.hippo.hippocafe.attribute.{Attribute, AttributeInfo}
import rip.hippo.hippocafe.attribute.impl.data.annotation.target.impl.{CatchTarget, EmptyTarget, FormalParameterTarget, LocalVariableTarget, OffsetTarget, SuperTypeTarget, ThrowsTarget, TypeArgumentTarget, TypeParameterBoundTarget, TypeParameterTarget}
import rip.hippo.hippocafe.attribute.impl.{AnnotationDefaultAttribute, BootstrapMethodsAttribute, CodeAttribute, ConstantValueAttribute, DeprecatedAttribute, EnclosingMethodAttribute, ExceptionsAttribute, InnerClassesAttribute, LineNumberTableAttribute, LocalVariableTableAttribute, LocalVariableTypeTableAttribute, MethodParametersAttribute, ModuleAttribute, ModuleMainClassAttribute, ModulePackagesAttribute, NestHostAttribute, NestMembersAttribute, PermittedSubclassesAttribute, RecordAttribute, RuntimeInvisibleAnnotationsAttribute, RuntimeInvisibleParameterAnnotationsAttribute, RuntimeInvisibleTypeAnnotationsAttribute, RuntimeVisibleAnnotationsAttribute, RuntimeVisibleParameterAnnotationsAttribute, RuntimeVisibleTypeAnnotationsAttribute, SignatureAttribute, SourceDebugExtensionAttribute, SourceFileAttribute, StackMapTableAttribute, SyntheticAttribute, UnknownAttribute}
import rip.hippo.hippocafe.attribute.impl.data.{BootstrapMethodsAttributeData, ClassesAttributeData, ExceptionTableAttributeData, LineNumberTableAttributeData, LocalVariableTableAttributeData, LocalVariableTypeTableAttributeData, MethodParametersAttributeData, RecordComponentInfo, annotation}
import rip.hippo.hippocafe.attribute.impl.data.annotation.{AnnotationAttributeData, ElementValuePairAnnotationData, ParameterAnnotationsData, TypeAnnotationData}
import rip.hippo.hippocafe.attribute.impl.data.annotation.value.AnnotationAttributeValue
import rip.hippo.hippocafe.attribute.impl.data.annotation.value.impl.data.ConstantAnnotationValueType
import rip.hippo.hippocafe.attribute.impl.data.annotation.value.impl.{AnnotationAnnotationValue, ArrayValueAnnotationValue, ClassInfoIndexAnnotationValue, ConstantAnnotationValue, EnumConstantAnnotationValue}
import rip.hippo.hippocafe.attribute.impl.data.module.{ExportsModuleData, OpensModuleData, ProvidesModuleData, RequiresModuleData}
import rip.hippo.hippocafe.constantpool.ConstantPool
import rip.hippo.hippocafe.constantpool.info.impl.{DynamicInfo, MethodHandleInfo, NameAndTypeInfo}
import rip.hippo.hippocafe.disassembler.instruction.constant.Constant
import rip.hippo.hippocafe.exception.HippoCafeException
import rip.hippo.hippocafe.io.reader.AttributeReader
import rip.hippo.hippocafe.stackmap.StackMapFrame
import rip.hippo.hippocafe.stackmap.impl.{AppendStackMapFrame, ChopStackMapFrame, FullStackMapFrame, SameExtendedStackMapFrame, SameLocalsExtendedStackMapFrame, SameLocalsStackMapFrame, SameStackMapFrame}
import rip.hippo.hippocafe.stackmap.verification.VerificationTypeInfo
import rip.hippo.hippocafe.stackmap.verification.impl.{DoubleVerificationTypeInfo, FloatVerificationTypeInfo, IntegerVerificationTypeInfo, LongVerificationTypeInfo, NullVerificationTypeInfo, ObjectVerificationTypeInfo, TopVerificationTypeInfo, UninitializedThisVerificationTypeInfo, UninitializedVerificationTypeInfo}

import java.io.{ByteArrayInputStream, DataInputStream}
import scala.collection.mutable.ListBuffer

/**
 * @author Hippo
 * @version 1.1.0, 8/15/21
 * @since 2.0.0
 */
final case class StandardAttributeReader() extends AttributeReader {
  override def read(parentStream: DataInputStream, inputStream: DataInputStream, constantPool: ConstantPool, oak: Boolean): AttributeInfo = {
    val attributeNameIndex = parentStream.readUnsignedShort()
    val attributeLength = parentStream.readInt()
    val name = constantPool.readUTF8(attributeNameIndex)


    val buffer = new Array[Byte](attributeLength)
    parentStream.readFully(buffer)

    try {

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
          case 7 => ObjectVerificationTypeInfo(constantPool.readString(u2))
          case 8 => UninitializedVerificationTypeInfo(u2)
          case x => throw new HippoCafeException(s"Unable to find type info for tag $x")
        }
      }

      def readAnnotationData: AnnotationAttributeData = {
        val typeIndex = u2
        val numberOfElementPairs = u2
        val elementValuePairs = new Array[ElementValuePairAnnotationData](numberOfElementPairs)
        (0 until numberOfElementPairs).foreach(i => elementValuePairs(i) = ElementValuePairAnnotationData(constantPool.readUTF8(u2), readAnnotationAttributeValue))
        AnnotationAttributeData(constantPool.readUTF8(typeIndex), elementValuePairs.toIndexedSeq)
      }

      def readAnnotationAttributeValue: AnnotationAttributeValue = {
        val tag = u1.toChar
        tag match {
          case 'B' |
               'C' |
               'D' |
               'F' |
               'I' |
               'J' |
               'S' |
               'Z' |
               's' =>
            ConstantAnnotationValue(ConstantAnnotationValueType.fromTag(tag).get, Constant.fromInfo(constantPool.info(u2)))
          case 'e' => EnumConstantAnnotationValue(constantPool.readUTF8(u2), constantPool.readUTF8(u2))
          case 'c' => ClassInfoIndexAnnotationValue(constantPool.readUTF8(u2))
          case '@' => AnnotationAnnotationValue(readAnnotationData)
          case '[' =>
            val numberOfValues = u2
            val values = new Array[AnnotationAttributeValue](numberOfValues)
            (0 until numberOfValues).foreach(i => values(i) = readAnnotationAttributeValue)
            ArrayValueAnnotationValue(values.toIndexedSeq)
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
                LocalVariableTarget(kind, tableLength, table.toIndexedSeq)
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
        val typePath = AnnotationTypePath(path.toIndexedSeq)
        val typeName = constantPool.readUTF8(u2)
        val numberOfElementValuePairs = u2
        val elementValuePairs = new Array[ElementValuePairAnnotationData](numberOfElementValuePairs)
        (0 until numberOfElementValuePairs).foreach(i => elementValuePairs(i) = ElementValuePairAnnotationData(constantPool.readUTF8(u2), readAnnotationAttributeValue))
        annotation.TypeAnnotationData(targetType, targetInfo, typePath, typeName, elementValuePairs.toIndexedSeq)
      }

      Attribute.fromName(name) match {
        case Some(attribute) =>
          import Attribute._
          attribute match {
            case CONSTANT_VALUE => ConstantValueAttribute(Constant.fromInfo(constantPool.info(u2)))
            case CODE =>
              val maxStack = if (oak) u1 else u2
              val maxLocals = if (oak) u1 else u2
              val codeLength = if (oak) u2 else u4
              val code = new Array[Byte](codeLength)
              bufferStream.readFully(code)
              val exceptionTableLength = u2
              val exceptionTable = new Array[ExceptionTableAttributeData](exceptionTableLength)
              (0 until exceptionTableLength).foreach(i => exceptionTable(i) = ExceptionTableAttributeData(u2, u2, u2, {
                val index = u2
                if (index == 0) null else constantPool.readString(index)
              }))
              val attributesCount = u2
              val attributes = new Array[AttributeInfo](attributesCount)
              (0 until attributesCount).foreach(i => attributes(i) = read(bufferStream, inputStream, constantPool, oak))
              CodeAttribute(oak, maxStack, maxLocals, ListBuffer.from(code), exceptionTableLength, ListBuffer.from(exceptionTable), ListBuffer.from(attributes))
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
              StackMapTableAttribute(ListBuffer.from(entries))
            case EXCEPTIONS =>
              val numberOfExceptions = u2
              val exceptionIndexTable = new Array[String](numberOfExceptions)
              (0 until numberOfExceptions).foreach(i => exceptionIndexTable(i) = constantPool.readString(u2))
              ExceptionsAttribute(ListBuffer.from(exceptionIndexTable))
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
              InnerClassesAttribute(ListBuffer.from(classes))
            case ENCLOSING_METHOD =>
              val className = constantPool.readString(u2)
              val methodIndex = u2
              var methodName = Option.empty[String]
              var methodType = Option.empty[String]
              if (methodIndex != 0) {
                val nameAndTypeInfo = constantPool.info(methodIndex).asInstanceOf[NameAndTypeInfo]
                methodName = Option(nameAndTypeInfo.name)
                methodType = Option(nameAndTypeInfo.descriptor)
              }
              EnclosingMethodAttribute(className, methodName, methodType)
            case SYNTHETIC => SyntheticAttribute()
            case SIGNATURE => SignatureAttribute(constantPool.readUTF8(u2))
            case SOURCE_FILE => SourceFileAttribute(constantPool.readUTF8(u2))
            case SOURCE_DEBUG_EXTENSION => SourceDebugExtensionAttribute(ListBuffer.from(buffer))
            case LINE_NUMBER_TABLE =>
              val lineNumberTableLength = u2
              val lineNumberTable = new Array[LineNumberTableAttributeData](lineNumberTableLength)
              (0 until lineNumberTableLength).foreach(i => lineNumberTable(i) = LineNumberTableAttributeData(u2, u2))
              LineNumberTableAttribute(ListBuffer.from(lineNumberTable))
            case LOCAL_VARIABLE_TABLE =>
              val localVariableTableLength = u2
              val localVariableTable = new Array[LocalVariableTableAttributeData](localVariableTableLength)
              (0 until localVariableTableLength).foreach(i => localVariableTable(i) = LocalVariableTableAttributeData(u2, u2, constantPool.readUTF8(u2), constantPool.readUTF8(u2), u2))
              LocalVariableTableAttribute(ListBuffer.from(localVariableTable))
            case LOCAL_VARIABLE_TYPE_TABLE =>
              val localVariableTypeTableLength = u2
              val localVariableTypeTable = new Array[LocalVariableTypeTableAttributeData](localVariableTypeTableLength)
              (0 until localVariableTypeTableLength).foreach(i => localVariableTypeTable(i) = LocalVariableTypeTableAttributeData(u2, u2, constantPool.readUTF8(u2), constantPool.readUTF8(u2), u2))
              LocalVariableTypeTableAttribute(ListBuffer.from(localVariableTypeTable))
            case DEPRECATED => DeprecatedAttribute()
            case RUNTIME_VISIBLE_ANNOTATIONS =>
              val numberOfAnnotations = u2
              val annotations = new Array[AnnotationAttributeData](numberOfAnnotations)
              (0 until numberOfAnnotations).foreach(i => annotations(i) = readAnnotationData)
              RuntimeVisibleAnnotationsAttribute(ListBuffer.from(annotations))
            case RUNTIME_INVISIBLE_ANNOTATIONS =>
              val numberOfAnnotations = u2
              val annotations = new Array[AnnotationAttributeData](numberOfAnnotations)
              (0 until numberOfAnnotations).foreach(i => annotations(i) = readAnnotationData)
              RuntimeInvisibleAnnotationsAttribute(ListBuffer.from(annotations))
            case RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS =>
              val numberOfParameters = u1
              val parameterAnnotations = new Array[ParameterAnnotationsData](numberOfParameters)
              (0 until numberOfParameters).foreach(i => {
                val numberOfAnnotations = u2
                val annotations = new Array[AnnotationAttributeData](numberOfAnnotations)
                (0 until numberOfAnnotations).foreach(k => annotations(k) = readAnnotationData)
                parameterAnnotations(i) = ParameterAnnotationsData(annotations.toIndexedSeq)
              })
              RuntimeVisibleParameterAnnotationsAttribute(ListBuffer.from(parameterAnnotations))
            case RUNTIME_INVISIBLE_PARAMETER_ANNOTATIONS =>
              val numberOfParameters = u1
              val parameterAnnotations = new Array[ParameterAnnotationsData](numberOfParameters)
              (0 until numberOfParameters).foreach(i => {
                val numberOfAnnotations = u2
                val annotations = new Array[AnnotationAttributeData](numberOfAnnotations)
                (0 until numberOfAnnotations).foreach(k => annotations(k) = readAnnotationData)
                parameterAnnotations(i) = ParameterAnnotationsData(annotations.toIndexedSeq)
              })
              RuntimeInvisibleParameterAnnotationsAttribute(ListBuffer.from(parameterAnnotations))
            case RUNTIME_VISIBLE_TYPE_ANNOTATIONS =>
              val numberOfAnnotations = u2
              val annotations = new Array[TypeAnnotationData](numberOfAnnotations)
              (0 until numberOfAnnotations).foreach(i => annotations(i) = readTypeAnnotationData)
              RuntimeVisibleTypeAnnotationsAttribute(ListBuffer.from(annotations))
            case RUNTIME_INVISIBLE_TYPE_ANNOTATIONS =>
              val numberOfAnnotations = u2
              val annotations = new Array[TypeAnnotationData](numberOfAnnotations)
              (0 until numberOfAnnotations).foreach(i => annotations(i) = readTypeAnnotationData)
              RuntimeInvisibleTypeAnnotationsAttribute(ListBuffer.from(annotations))
            case ANNOTATION_DEFAULT => AnnotationDefaultAttribute(readAnnotationAttributeValue)
            case BOOTSTRAP_METHODS =>
              val numberOfBootstrapMethods = u2
              val bootstrapMethods = new Array[BootstrapMethodsAttributeData](numberOfBootstrapMethods)
              (0 until numberOfBootstrapMethods).foreach(i => {
                val bootstrapMethodRef = constantPool.info(u2).asInstanceOf[MethodHandleInfo]
                val numberOfBootstrapArguments = u2
                val bootstrapArguments = ListBuffer[Constant[?]]()
                (0 until numberOfBootstrapArguments).foreach(i => bootstrapArguments += Constant.fromInfo(constantPool.info(u2)))
                bootstrapMethods(i) = BootstrapMethodsAttributeData(bootstrapMethodRef, bootstrapArguments)
              })
              val bsmAttribute = BootstrapMethodsAttribute(ListBuffer.from(bootstrapMethods))

              constantPool.info.values.foreach {
                case dynamicInfo: DynamicInfo =>
                  dynamicInfo.bootstrapMethodsAttributeData = bootstrapMethods(dynamicInfo.bsmAttributeIndex)
                case _ =>
              }

              bsmAttribute
            case METHOD_PARAMETERS =>
              val parametersCount = u1
              val parameters = new Array[MethodParametersAttributeData](parametersCount)
              val nameIndex = u2
              val access = u2
              var name = Option.empty[String]
              if (nameIndex != 0) {
                name = Option(constantPool.readUTF8(nameIndex))
              }
              (0 until parametersCount).foreach(i => parameters(i) = MethodParametersAttributeData(name, access))
              MethodParametersAttribute(ListBuffer.from(parameters))
            case MODULE =>
              val moduleNameIndex = u2
              val moduleFlags = u2
              val moduleVersionIndex = u2

              val moduleName = constantPool.readString(moduleNameIndex)
              val moduleVersion = if (moduleVersionIndex == 0) Option.empty else Option(constantPool.readString(moduleVersionIndex))

              val requiresCount = u2
              val requires = new Array[RequiresModuleData](requiresCount)
              (0 until requiresCount).foreach(i => {
                val require = constantPool.readString(u2)
                val flags = u2
                val index = u2
                val version = if (index == 0) Option.empty else Option(constantPool.readUTF8(index))
                requires(i) = RequiresModuleData(require, flags, version)
              })

              val exportsCount = u2
              val exports = new Array[ExportsModuleData](exportsCount)
              (0 until exportsCount).foreach(i => {
                val exportsIndex = constantPool.readString(u2)
                val exportsFlags = u2
                val exportsToCount = u2
                val exportsToIndex = new Array[String](exportsToCount)
                (0 until exportsToCount).foreach(k => exportsToIndex(k) = constantPool.readString(u2))
                exports(i) = ExportsModuleData(exportsIndex, exportsFlags, ListBuffer.from(exportsToIndex))
              })

              val opensCount = u2
              val opens = new Array[OpensModuleData](opensCount)
              (0 until opensCount).foreach(i => {
                val opensIndex = constantPool.readString(u2)
                val opensFlags = u2
                val opensToCount = u2
                val opensToIndex = new Array[String](opensToCount)
                (0 until opensToCount).foreach(k => opensToIndex(k) = constantPool.readString(u2))
                opens(i) = OpensModuleData(opensIndex, opensFlags, ListBuffer.from(opensToIndex))
              })

              val usesCount = u2
              val usesIndex = new Array[String](usesCount)
              (0 until usesCount).foreach(i => usesIndex(i) = constantPool.readString(u2))

              val providesCount = u2
              val provides = new Array[ProvidesModuleData](providesCount)
              (0 until providesCount).foreach(i => {
                val providesIndex = constantPool.readString(u2)
                val providesWithCount = u2
                val providesWithIndex = new Array[String](providesWithCount)
                (0 until providesWithCount).foreach(k => providesWithIndex(k) = constantPool.readString(u2))
                provides(i) = ProvidesModuleData(providesIndex, ListBuffer.from(providesWithIndex))
              })
              ModuleAttribute(moduleName, moduleFlags, moduleVersion, ListBuffer.from(requires), ListBuffer.from(exports), ListBuffer.from(opens), ListBuffer.from(usesIndex), ListBuffer.from(provides))
            case MODULE_PACKAGES =>
              val packageCount = u2
              val packageIndex = new Array[String](packageCount)
              (0 until packageCount).foreach(i => packageIndex(i) = constantPool.readString(u2))
              ModulePackagesAttribute(ListBuffer.from(packageIndex))
            case MODULE_MAIN_CLASS => ModuleMainClassAttribute(constantPool.readString(u2))
            case NEST_HOST => NestHostAttribute(constantPool.readString(u2))
            case NEST_MEMBERS =>
              val numberOfClasses = u2
              val classes = new Array[String](numberOfClasses)
              (0 until numberOfClasses).foreach(i => classes(i) = constantPool.readString(u2))
              NestMembersAttribute(ListBuffer.from(classes))
            case RECORD =>
              val componentsCount = u2
              val components = new Array[RecordComponentInfo](componentsCount)
              (0 until componentsCount).foreach(i => {
                val name = constantPool.readUTF8(u2)
                val descriptor = constantPool.readUTF8(u2)
                val attributesCount = u2
                val attributes = new Array[AttributeInfo](attributesCount)
                (0 until attributesCount).foreach(i => attributes(i) = read(bufferStream, inputStream, constantPool, oak))
                components(i) = RecordComponentInfo(name, descriptor, ListBuffer.from(attributes))
              })
              RecordAttribute(ListBuffer.from(components))
            case PERMITTED_SUBCLASSES =>
              val numberOfClasses = u2
              val classes = new Array[String](numberOfClasses)
              (0 until numberOfClasses).foreach(i => {
                classes(i) = constantPool.readString(u2)
              })
              PermittedSubclassesAttribute(ListBuffer.from(classes))
            case _ => UnknownAttribute(name, ListBuffer.from(buffer))
          }
        case None => UnknownAttribute(name, ListBuffer.from(buffer))
      }
    } catch {
      case _: Throwable =>
        UnknownAttribute(name, ListBuffer.from(buffer))
    } finally if (parentStream != null && parentStream != inputStream) parentStream.close()
  }
}
