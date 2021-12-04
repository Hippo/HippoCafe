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

import java.io.{ByteArrayInputStream, DataInputStream, InputStream}
import rip.hippo.hippocafe.constantpool.ConstantPoolKind.*
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
import rip.hippo.hippocafe.io.reader.*
import rip.hippo.hippocafe.io.reader.impl._
import rip.hippo.hippocafe.stackmap.StackMapFrame
import rip.hippo.hippocafe.stackmap.impl.{AppendStackMapFrame, ChopStackMapFrame, FullStackMapFrame, SameExtendedStackMapFrame, SameLocalsExtendedStackMapFrame, SameLocalsStackMapFrame, SameStackMapFrame}
import rip.hippo.hippocafe.stackmap.verification.VerificationTypeInfo
import rip.hippo.hippocafe.stackmap.verification.impl.{DoubleVerificationTypeInfo, FloatVerificationTypeInfo, IntegerVerificationTypeInfo, LongVerificationTypeInfo, NullVerificationTypeInfo, ObjectVerificationTypeInfo, TopVerificationTypeInfo, UninitializedThisVerificationTypeInfo, UninitializedVerificationTypeInfo}
import rip.hippo.hippocafe.version.MajorClassFileVersion

import scala.collection.mutable.{ArrayBuffer, ListBuffer}


/**
 * @author Hippo
 * @version 1.1.0, 8/1/20
 * @since 1.0.0
 */
final class ClassReader(bytecode: InputStream | Array[Byte],
                        lowLevel: Boolean = false,
                        classHeaderReader: ClassHeaderReader = StandardClassHeaderReader(),
                        constantPoolReader: ConstantPoolReader = StandardConstantPoolReader(),
                        classMetaReader: ClassMetaReader = StandardClassMetaReader(),
                        interfaceReader: InterfaceReader = StandardInterfaceReader(),
                        attributeReader: AttributeReader = StandardAttributeReader(),
                        fieldReader: FieldReader = StandardFieldReader(),
                        methodReader: MethodReader = StandardMethodReader(),
                        classAttributeReader: ClassAttributesReader = StandardClassAttributeReader()) extends AutoCloseable {


  private val inputStream = bytecode match {
    case parentStream: InputStream => new DataInputStream(parentStream)
    case byteArray: Array[Byte] => new DataInputStream(new ByteArrayInputStream(byteArray))
  }

  private val headerData = classHeaderReader.read(inputStream)

  private val minorVersion = headerData.minorVersion
  private val majorVersion = headerData.majorVersion
  private val oak = {
    val version = MajorClassFileVersion.JDK1_1.version
    majorVersion < version || (majorVersion == version && minorVersion < 3)
  }


  val constantPool: ConstantPool = new ConstantPool

  constantPoolReader.read(inputStream, constantPool)

  private val classMetaData = classMetaReader.read(inputStream)

  private val accessMask = classMetaData.accessMask
  private val className = constantPool.readString(classMetaData.classNameIndex)
  private val superName = if (classMetaData.superNameIndex == 0) null else constantPool.readString(classMetaData.superNameIndex)

  val classFile: ClassFile = new ClassFile(MajorClassFileVersion.fromVersion(majorVersion).get, className, superName, accessMask)

  classFile.lowLevel = lowLevel
  classFile.minorVersion = minorVersion
  classFile.constantPool = Option(constantPool)

  private val interfaceData = interfaceReader.read(inputStream)

  classFile.interfaces.addAll(interfaceData.interfaceIndexes.map(i => constantPool.readString(i)))

  classFile.fields.addAll(fieldReader.read(inputStream, attributeReader, constantPool, oak))
  classFile.methods.addAll(methodReader.read(inputStream, attributeReader, constantPool, oak))
  classFile.attributes.addAll(classAttributeReader.read(inputStream, attributeReader, constantPool, oak))

  if (!lowLevel) {
    val bootstrapMethodsAttribute = classFile.attributes.find(_.isInstanceOf[BootstrapMethodsAttribute]) match {
      case Some(value) =>
        classFile.attributes -= value
        Option(value.asInstanceOf[BootstrapMethodsAttribute])
      case None => Option.empty[BootstrapMethodsAttribute]
    }
    classFile.methods.foreach(methodInfo => {
      val copyMethodAttributes = ListBuffer[AttributeInfo]()
      copyMethodAttributes ++= methodInfo.attributes
      try {
        CodeDisassembler.disassemble(methodInfo, constantPool, bootstrapMethodsAttribute)
      } catch {
        case _: Throwable =>
          methodInfo.attributes.clear()
          methodInfo.attributes ++= copyMethodAttributes
      }
    })
  }

  override def close(): Unit = inputStream.close()
}