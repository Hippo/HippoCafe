# 12 — Class Hierarchy, Assignability, and Reference-Type Merging

## Goals and boundaries

Provide a lazy, cacheable model of superclass and interface relations that is sufficient for JVM verification and useful to later analyses. The class hierarchy consumes `ClassResolver` metadata and produces semantic answers about **reference types**. It must not parse whole JARs into `ClassFile` objects, instantiate Java classes, or use host JVM reflection.

Do not conflate three questions:

1. **Resolution:** which class definition does this binary name mean in the configured context?
2. **Assignability:** may a value of reference type `source` be used in a location requiring `destination`?
3. **Join:** what representable verification type(s) may safely describe values arriving from two control-flow predecessors?

A boolean `false` from assignability means "not assignable", **not** "the resolver failed".

## Type identities

A reference type is one of:

- A resolved class or interface `ClassName` in a specific resolution snapshot.
- An array type with a component that may itself be a primitive or reference type.
- The special verifier `null` type (in `VerificationType`, not a loadable class).

Primitive types are field-descriptor types, not classpath entries. Parse JVM descriptors centrally rather than treating raw descriptor strings as arbitrary class names. For arrays, preserve component type and dimensions; distinguish `[I` from `[Ljava/lang/Object;` and `[[I`. Keep a coherent distinction between class identity and simple binary-name spelling.

`ClassInfo` contains a declared superclass (absent for `java/lang/Object` under ordinary class semantics), ordered direct superinterfaces, class/interface access bits, version, and provenance. The hierarchy caches resolved metadata, not whole method bodies.

## Required operations

```cpp
class ClassHierarchy {
public:
    std::expected<ClassInfoView, HierarchyError> resolve(ClassName name) const;

    std::expected<bool, HierarchyError> is_assignable(
        ReferenceType source,
        ReferenceType destination
    ) const;

    std::expected<JoinResult, HierarchyError> join_reference_types(
        ReferenceType a,
        ReferenceType b
    ) const;
};
```

`JoinResult` should distinguish a unique selected type from ambiguous candidates or an explicit incompatible/unknown condition. The frame engine needs an actual **verification-representable** type and must not silently assume `Object` is a universally useful substitute. If a deterministic conservative join is chosen, it must be followed by verification of the resulting flow and must not incorrectly accept invalid method use.

## Superclass and interface graph

- For classes, follow the declared superclass to `java/lang/Object` and include all transitive implemented interfaces, including interfaces inherited from superclasses.
- For interfaces, follow extended interfaces, and account for their JVM reference relationship with `java/lang/Object` as specified by the relevant verification rules.
- Reject cycles and invalid structural contradictions using a three-color DFS or equivalent, with a cycle path in the error.
- Do not silently ignore missing parent classes or interfaces. A class known to exist does **not** make its unknown ancestor safe to treat as Object.
- Detect inconsistent repeated declarations by class name and source identity. Resolution should be stable across all queries within one snapshot.
- Do not eagerly traverse unrelated classes. Resolve ancestor chains and interface closures only as needed.

Provide indexed ancestor/interface sets when hot queries justify them, and cache assignability for pairs of resolved type IDs. Results depend on the hierarchy snapshot and must be invalidated when its inputs change.

## Assignability rules (examples, not a substitute for JVMS)

```text
String -> Object                         true
ArrayList -> List                       true, if implemented in selected hierarchy
List -> ArrayList                       false
String[] -> Object                      true
String[] -> Cloneable                   true
String[] -> java/io/Serializable        true
String[] -> Object[]                    true
int[] -> Object[]                       false
int[][] -> Object[]                     true (component int[] is a reference)
int[] -> int[]                          true
int[] -> long[]                         false
null -> any valid reference type        true (verification semantics)
```

Array covariance only applies when the component types are references and the components are assignable according to JVM rules. For arrays with primitive component types, require exact primitive component matches at that level; nested arrays are themselves references. Arrays are synthesizable reference types and are assignable to `Object`, `Cloneable`, and `Serializable` without requiring a `.class` archive entry for the array itself. Resolve the named class/interface targets when required; do not create fake archive records for arrays.

Primitive int-like descriptors (`Z`, `B`, `C`, `S`, `I`) are represented by the verifier's `Integer` type, but do not merge a primitive with an object reference merely because both occupy one slot.

## Frame joins require more than 'walk the superclasses'

For a class-only tree, the nearest common superclass is straightforward:

```text
Dog <: Animal    Cat <: Animal
join(Dog, Cat) = Animal
```

But Java interfaces make least upper bounds potentially **non-unique**:

```java
interface I {}
interface J {}
class A implements I, J {}
class B implements I, J {}
```

Both `I` and `J` are valid common supertypes of `A` and `B`, and neither is more specific than the other. A single JVM `Object_variable_info` encodes one reference name, **not** an intersection of unrelated interfaces. Therefore:

- Do not claim that `common_supertype(a, b)` always yields a unique most-specific type.
- Do not always return `Object` as soon as an interface appears: that may erase type information needed by subsequent `invokeinterface` or other operations.
- Identify assignability and common candidates through the hierarchy. If there is a unique expressible most-specific common type, use it.
- If there are several incomparable candidates, use a documented verifier-safe strategy: retain ambiguity until instruction-use constraints can select an expressible type, or conservatively choose one, validate all subsequent uses, and report `AmbiguousTypeJoin`/`UnverifiableTypeJoin` if no single representable frame type works.
- Never fabricate an intersection `VerificationType` for `StackMapTable`; unresolved internal constraints must be discharged before writing.

An internal richer type constraint set is permissible but is an **analysis-only** value, not the serialized frame format. Test that generated frames are accepted by the target JVM, including cases where common superinterfaces matter and cases where no single frame type can satisfy all downstream uses.

## Additional verification-type joins

Reference joins are only part of frame merging. The `FrameAnalyzer` handles `Null`, `Top`/unusable locals, primitive types, category-two slots and uninitialized identities. In particular:

- `Null` joined with initialized reference `T` can yield `T`.
- Two distinct `NEW` allocation sites are **not** interchangeable initialized references; an uninitialized value is identified by a specific instruction.
- `UninitializedThis` has constructor-specific constraints; do not merge it as though it were the initialized current class.
- Incompatible stack shapes/primitive categories are errors; permissive local-variable merging may instead make a local unusable, according to JVM rules.

Keep these special rules in `frame_merge.cpp`; the public hierarchy interface should not need to know about current stack heights or constructor flags.

## Resolution and hierarchy errors

At minimum report `NotFound`, `MalformedMetadata`, `SourceConflict`, `HierarchyCycle`, `UnsupportedClassVersion`, `InvalidParent`, and resource-limit failure with the type/source/path involved. Do not cache transient failures as permanent across changed source snapshots; per-snapshot negative caching is acceptable.

Implement the complete assignability rules using the applicable JVM specification, including interface/array subtleties. The examples above intentionally cover common mistakes but are not an exhaustive verifier definition.

## Tests

- Single and multiple inheritance through classes/interfaces; deep and diamond interface graphs; interface inheritance through a superclass.
- Same class name in two archives; selected classpath precedence; cache invalidation on snapshot change.
- Missing direct superclass, missing transitive interface, cycles, incorrect class path entry name; deterministic errors rather than Object fallback.
- Arrays of primitives, reference arrays, nested arrays, array-to-special interfaces, null values.
- Unique superclass merge, shared-interface merge, unrelated interfaces, incomparable common interfaces.
- Comparable answer sets with `Class.isAssignableFrom` in a controlled JVM class-loader fixture where the configured hierarchy matches; comparisons must account for JVM loader identity.

## References

JVMS §2.4 (reference types), §4.3 (descriptors), §4.10.1.2 (verification types), and §5.3 (class loading/identity). A classpath-style resolver is an explicit abstraction and is **not** a simulation of all class loaders and module layers.
