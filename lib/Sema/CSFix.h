//===--- CSFix.h - Constraint Fixes ---------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2018 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file provides necessary abstractions for constraint fixes.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SEMA_CSFIX_H
#define SWIFT_SEMA_CSFIX_H

#include "swift/AST/Decl.h"
#include "swift/AST/Expr.h"
#include "swift/AST/Identifier.h"
#include "swift/AST/Type.h"
#include "swift/AST/Types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/TrailingObjects.h"
#include <string>

namespace llvm {
class raw_ostream;
}

namespace swift {

class SourceManager;

namespace constraints {

class OverloadChoice;
class ConstraintSystem;
class ConstraintLocator;
class ConstraintLocatorBuilder;
class Solution;

/// Describes the kind of fix to apply to the given constraint before
/// visiting it.
enum class FixKind : uint8_t {
  /// Introduce a '!' to force an optional unwrap.
  ForceOptional,

  /// Unwrap an optional base when we have a member access.
  UnwrapOptionalBase,
  UnwrapOptionalBaseWithOptionalResult,

  /// Append 'as! T' to force a downcast to the specified type.
  ForceDowncast,

  /// Introduce a '&' to take the address of an lvalue.
  AddressOf,
  /// Remove extraneous use of `&`.
  RemoveAddressOf,

  /// Replace a coercion ('as') with a forced checked cast ('as!').
  CoerceToCheckedCast,

  /// Mark function type as explicitly '@escaping'.
  ExplicitlyEscaping,

  /// Arguments have labeling failures - missing/extraneous or incorrect
  /// labels attached to the, fix it by suggesting proper labels.
  RelabelArguments,

  /// Treat rvalue as lvalue
  TreatRValueAsLValue,

  /// Add a new conformance to the type to satisfy a requirement.
  AddConformance,

  /// Skip same-type generic requirement constraint,
  /// and assume that types are equal.
  SkipSameTypeRequirement,

  /// Skip superclass generic requirement constraint,
  /// and assume that types are related.
  SkipSuperclassRequirement,

  /// Fix up one of the sides of conversion to make it seem
  /// like the types are aligned.
  ContextualMismatch,

  /// Fix up the generic arguments of two types so they match each other.
  GenericArgumentsMismatch,

  /// Fix up @autoclosure argument to the @autoclosure parameter,
  /// to for a call to be able to foward it properly, since
  /// @autoclosure conversions are unsupported starting from
  /// Swift version 5.
  AutoClosureForwarding,

  /// Remove `!` or `?` because base is not an optional type.
  RemoveUnwrap,

  /// Add explicit `()` at the end of function or member to call it.
  InsertCall,

  /// Add '$' or '_' to refer to the property wrapper or storage instead
  /// of the wrapped property type.
  UsePropertyWrapper,

  /// Remove '$' or '_' to refer to the wrapped property type instead of
  /// the storage or property wrapper.
  UseWrappedValue,

  /// Instead of spelling out `subscript` directly, use subscript operator.
  UseSubscriptOperator,

  /// Requested name is not associated with a give base type,
  /// fix this issue by pretending that member exists and matches
  /// given arguments/result types exactly.
  DefineMemberBasedOnUse,

  /// Allow access to type member on instance or instance member on type
  AllowTypeOrInstanceMember,

  /// Allow expressions where 'mutating' method is only partially applied,
  /// which means either not applied at all e.g. `Foo.bar` or only `Self`
  /// is applied e.g. `foo.bar` or `Foo.bar(&foo)`.
  ///
  /// Allow expressions where initializer call (either `self.init` or
  /// `super.init`) is only partially applied.
  AllowInvalidPartialApplication,

  /// Non-required constructors may not be not inherited. Therefore when
  /// constructing a class object, either the metatype must be statically
  /// derived (rather than an arbitrary value of metatype type) or the
  /// referenced constructor must be required.
  AllowInvalidInitRef,

  /// Allow a tuple to be destructured with mismatched arity, or mismatched
  /// types.
  AllowTupleTypeMismatch,

  /// Allow an invalid member access on a value of protocol type as if
  /// that protocol type were a generic constraint requiring conformance
  /// to that protocol.
  AllowMemberRefOnExistential,

  /// If there are fewer arguments than parameters, let's fix that up
  /// by adding new arguments to the list represented as type variables.
  AddMissingArguments,

  /// Allow single tuple closure parameter destructuring into N arguments.
  AllowClosureParameterDestructuring,

  /// If there is out-of-order argument, let's fix that by re-ordering.
  MoveOutOfOrderArgument,

  /// If there is a matching inaccessible member - allow it as if there
  /// no access control.
  AllowInaccessibleMember,

  /// Allow KeyPaths to use AnyObject as root type
  AllowAnyObjectKeyPathRoot,

  /// Using subscript references in the keypath requires that each
  /// of the index arguments to be Hashable.
  TreatKeyPathSubscriptIndexAsHashable,

  /// Allow an invalid reference to a member declaration as part
  /// of a key path component.
  AllowInvalidRefInKeyPath,

  /// Remove `return` or default last expression of single expression
  /// function to `Void` to conform to expected result type.
  RemoveReturn,

  /// Generic parameters could not be inferred and have to be explicitly
  /// specified in the source. This fix groups all of the missing arguments
  /// associated with single declaration.
  ExplicitlySpecifyGenericArguments,

  /// Skip any unhandled constructs that occur within a closure argument that
  /// matches up with a
  /// parameter that has a function builder.
  SkipUnhandledConstructInFunctionBuilder,

  /// Allow invalid reference to a member declared as `mutating`
  /// when base is an r-value type.
  AllowMutatingMemberOnRValueBase,

  /// Allow a single tuple parameter to be matched with N arguments
  /// by forming all of the given arguments into a single tuple.
  AllowTupleSplatForSingleParameter,

  /// Allow a single argument type mismatch. This is the most generic
  /// failure related to argument-to-parameter conversions.
  AllowArgumentTypeMismatch,

  /// Explicitly construct type conforming to `RawRepresentable` protocol
  /// via forming `Foo(rawValue:)` instead of using its `RawValue` directly.
  ExplicitlyConstructRawRepresentable,

  /// Use raw value type associated with raw representative accessible
  /// using `.rawValue` member.
  UseValueTypeOfRawRepresentative,
  /// If an array was passed to a variadic argument, give a specific diagnostic
  /// and offer to drop the brackets if it's a literal.
  ExpandArrayIntoVarargs,
};

class ConstraintFix {
  ConstraintSystem &CS;
  FixKind Kind;
  ConstraintLocator *Locator;

  /// Determines whether this fix is simplify a warning which doesn't
  /// require immediate source changes.
  bool IsWarning;

public:
  ConstraintFix(ConstraintSystem &cs, FixKind kind, ConstraintLocator *locator,
                bool warning = false)
      : CS(cs), Kind(kind), Locator(locator), IsWarning(warning) {}

  virtual ~ConstraintFix();

  FixKind getKind() const { return Kind; }

  bool isWarning() const { return IsWarning; }

  virtual std::string getName() const = 0;

  /// Diagnose a failure associated with this fix given
  /// root expression and information from constraint system.
  virtual bool diagnose(Expr *root, bool asNote = false) const = 0;

  void print(llvm::raw_ostream &Out) const;

  LLVM_ATTRIBUTE_DEPRECATED(void dump() const
                              LLVM_ATTRIBUTE_USED,
                            "only for use within the debugger");

  /// Retrieve anchor expression associated with this fix.
  /// NOTE: such anchor comes directly from locator without
  /// any simplication attempts.
  Expr *getAnchor() const;
  ConstraintLocator *getLocator() const { return Locator; }

protected:
  ConstraintSystem &getConstraintSystem() const { return CS; }
};

/// Introduce a '!' to force an optional unwrap.
class ForceOptional final : public ConstraintFix {
  Type BaseType;
  Type UnwrappedType;

  ForceOptional(ConstraintSystem &cs, Type baseType, Type unwrappedType,
                ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::ForceOptional, locator), BaseType(baseType),
        UnwrappedType(unwrappedType) {
    assert(baseType && "Base type must not be null");
    assert(unwrappedType && "Unwrapped type must not be null");
  }

public:
  std::string getName() const override { return "force optional"; }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static ForceOptional *create(ConstraintSystem &cs, Type baseType,
                               Type unwrappedType, ConstraintLocator *locator);
};

/// Unwrap an optional base when we have a member access.
class UnwrapOptionalBase final : public ConstraintFix {
  DeclName MemberName;

  UnwrapOptionalBase(ConstraintSystem &cs, FixKind kind, DeclName member,
                     ConstraintLocator *locator)
      : ConstraintFix(cs, kind, locator), MemberName(member) {
    assert(kind == FixKind::UnwrapOptionalBase ||
           kind == FixKind::UnwrapOptionalBaseWithOptionalResult);
  }

public:
  std::string getName() const override {
    return "unwrap optional base of member lookup";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static UnwrapOptionalBase *create(ConstraintSystem &cs, DeclName member,
                                    ConstraintLocator *locator);

  static UnwrapOptionalBase *
  createWithOptionalResult(ConstraintSystem &cs, DeclName member,
                           ConstraintLocator *locator);
};

// Treat rvalue as if it was an lvalue
class TreatRValueAsLValue final : public ConstraintFix {
  TreatRValueAsLValue(ConstraintSystem &cs, ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::TreatRValueAsLValue, locator) {}

public:
  std::string getName() const override { return "treat rvalue as lvalue"; }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static TreatRValueAsLValue *create(ConstraintSystem &cs,
                                     ConstraintLocator *locator);
};

/// Mark function type as explicitly '@escaping'.
class MarkExplicitlyEscaping final : public ConstraintFix {
  /// Sometimes function type has to be marked as '@escaping'
  /// to be converted to some other generic type.
  Type ConvertTo;

  MarkExplicitlyEscaping(ConstraintSystem &cs, ConstraintLocator *locator,
                         Type convertingTo = Type())
      : ConstraintFix(cs, FixKind::ExplicitlyEscaping, locator),
        ConvertTo(convertingTo) {}

public:
  std::string getName() const override { return "add @escaping"; }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static MarkExplicitlyEscaping *create(ConstraintSystem &cs,
                                        ConstraintLocator *locator,
                                        Type convertingTo = Type());
};

/// Arguments have labeling failures - missing/extraneous or incorrect
/// labels attached to the, fix it by suggesting proper labels.
class RelabelArguments final
    : public ConstraintFix,
      private llvm::TrailingObjects<RelabelArguments, Identifier> {
  friend TrailingObjects;

  unsigned NumLabels;

  RelabelArguments(ConstraintSystem &cs,
                   llvm::ArrayRef<Identifier> correctLabels,
                   ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::RelabelArguments, locator),
        NumLabels(correctLabels.size()) {
    std::uninitialized_copy(correctLabels.begin(), correctLabels.end(),
                            getLabelsBuffer().begin());
  }

public:
  std::string getName() const override { return "re-label argument(s)"; }

  ArrayRef<Identifier> getLabels() const {
    return {getTrailingObjects<Identifier>(), NumLabels};
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static RelabelArguments *create(ConstraintSystem &cs,
                                  llvm::ArrayRef<Identifier> correctLabels,
                                  ConstraintLocator *locator);

private:
  MutableArrayRef<Identifier> getLabelsBuffer() {
    return {getTrailingObjects<Identifier>(), NumLabels};
  }
};

/// Add a new conformance to the type to satisfy a requirement.
class MissingConformance final : public ConstraintFix {
  // Determines whether given protocol type comes from the context e.g.
  // assignment destination or argument comparison.
  bool IsContextual;

  Type NonConformingType;
  // This could either be a protocol or protocol composition.
  Type ProtocolType;

  MissingConformance(ConstraintSystem &cs, bool isContextual, Type type,
                     Type protocolType, ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::AddConformance, locator),
        IsContextual(isContextual), NonConformingType(type),
        ProtocolType(protocolType) {}

public:
  std::string getName() const override {
    return "add missing protocol conformance";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static MissingConformance *forRequirement(ConstraintSystem &cs, Type type,
                                            Type protocolType,
                                            ConstraintLocator *locator);

  static MissingConformance *forContextual(ConstraintSystem &cs, Type type,
                                           Type protocolType,
                                           ConstraintLocator *locator);

  Type getNonConformingType() { return NonConformingType; }

  Type getProtocolType() { return ProtocolType; }
};

/// Skip same-type generic requirement constraint,
/// and assume that types are equal.
class SkipSameTypeRequirement final : public ConstraintFix {
  Type LHS, RHS;

  SkipSameTypeRequirement(ConstraintSystem &cs, Type lhs, Type rhs,
                          ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::SkipSameTypeRequirement, locator), LHS(lhs),
        RHS(rhs) {}

public:
  std::string getName() const override {
    return "skip same-type generic requirement";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  Type lhsType() { return LHS; }
  Type rhsType() { return RHS; }

  static SkipSameTypeRequirement *create(ConstraintSystem &cs, Type lhs,
                                         Type rhs, ConstraintLocator *locator);
};

/// Skip 'superclass' generic requirement constraint,
/// and assume that types are equal.
class SkipSuperclassRequirement final : public ConstraintFix {
  Type LHS, RHS;

  SkipSuperclassRequirement(ConstraintSystem &cs, Type lhs, Type rhs,
                            ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::SkipSuperclassRequirement, locator),
        LHS(lhs), RHS(rhs) {}

public:
  std::string getName() const override {
    return "skip superclass generic requirement";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  Type subclassType() { return LHS; }
  Type superclassType() { return RHS; }

  static SkipSuperclassRequirement *
  create(ConstraintSystem &cs, Type lhs, Type rhs, ConstraintLocator *locator);
};

/// For example: Sometimes type returned from the body of the
/// closure doesn't match expected contextual type:
///
/// func foo(_: () -> Int) {}
/// foo { "ultimate question" }
///
/// Body of the closure produces `String` type when `Int` is expected
/// by the context.
class ContextualMismatch : public ConstraintFix {
  Type LHS, RHS;

protected:
  ContextualMismatch(ConstraintSystem &cs, Type lhs, Type rhs,
                     ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::ContextualMismatch, locator), LHS(lhs),
        RHS(rhs) {}
  ContextualMismatch(ConstraintSystem &cs, FixKind kind, Type lhs, Type rhs,
                     ConstraintLocator *locator, bool warning = false)
      : ConstraintFix(cs, kind, locator, warning), LHS(lhs), RHS(rhs) {}

public:
  std::string getName() const override { return "fix contextual mismatch"; }

  Type getFromType() const { return LHS; }
  Type getToType() const { return RHS; }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static ContextualMismatch *create(ConstraintSystem &cs, Type lhs, Type rhs,
                                    ConstraintLocator *locator);
};

/// This is a contextual mismatch between throwing and non-throwing
/// function types, repair it by dropping `throws` attribute.
class DropThrowsAttribute final : public ContextualMismatch {
  DropThrowsAttribute(ConstraintSystem &cs, FunctionType *fromType,
                      FunctionType *toType, ConstraintLocator *locator)
      : ContextualMismatch(cs, fromType, toType, locator) {
    assert(fromType->throws() != toType->throws());
  }

public:
  std::string getName() const override { return "drop 'throws' attribute"; }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static DropThrowsAttribute *create(ConstraintSystem &cs,
                                     FunctionType *fromType,
                                     FunctionType *toType,
                                     ConstraintLocator *locator);
};

/// Append 'as! T' to force a downcast to the specified type.
class ForceDowncast final : public ContextualMismatch {
  ForceDowncast(ConstraintSystem &cs, Type fromType, Type toType,
                ConstraintLocator *locator)
      : ContextualMismatch(cs, FixKind::ForceDowncast, fromType, toType,
                           locator) {}

public:
  std::string getName() const override;

  bool diagnose(Expr *root, bool asNote = false) const override;

  static ForceDowncast *create(ConstraintSystem &cs, Type fromType, Type toType,
                               ConstraintLocator *locator);
};

/// Introduce a '&' to take the address of an lvalue.
class AddAddressOf final : public ContextualMismatch {
  AddAddressOf(ConstraintSystem &cs, Type argTy, Type paramTy,
               ConstraintLocator *locator)
      : ContextualMismatch(cs, FixKind::AddressOf, argTy, paramTy, locator) {}

public:
  std::string getName() const override { return "add address-of"; }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static AddAddressOf *create(ConstraintSystem &cs, Type argTy, Type paramTy,
                              ConstraintLocator *locator);
};

class RemoveAddressOf final : public ContextualMismatch {
  RemoveAddressOf(ConstraintSystem &cs, Type lhs, Type rhs,
                  ConstraintLocator *locator)
      : ContextualMismatch(cs, FixKind::RemoveAddressOf, lhs, rhs, locator) {}

public:
  std::string getName() const override {
    return "remove extraneous use of `&`";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static RemoveAddressOf *create(ConstraintSystem &cs, Type lhs, Type rhs,
                                 ConstraintLocator *locator);
};

/// Detect situations where two type's generic arguments must
/// match but are not convertible e.g.
///
/// ```swift
/// struct F<G> {}
/// let _:F<Int> = F<Bool>()
/// ```
class GenericArgumentsMismatch final
    : public ContextualMismatch,
      private llvm::TrailingObjects<GenericArgumentsMismatch, unsigned> {
  friend TrailingObjects;

  unsigned NumMismatches;

protected:
  GenericArgumentsMismatch(ConstraintSystem &cs, Type actual, Type required,
                           llvm::ArrayRef<unsigned> mismatches,
                           ConstraintLocator *locator)
      : ContextualMismatch(cs, FixKind::GenericArgumentsMismatch, actual,
                           required, locator),
        NumMismatches(mismatches.size()) {
    assert(actual->is<BoundGenericType>());
    assert(required->is<BoundGenericType>());
    std::uninitialized_copy(mismatches.begin(), mismatches.end(),
                            getMismatchesBuf().begin());
  }

public:
  std::string getName() const override {
    return "fix generic argument mismatch";
  }

  ArrayRef<unsigned> getMismatches() const {
    return {getTrailingObjects<unsigned>(), NumMismatches};
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static GenericArgumentsMismatch *create(ConstraintSystem &cs, Type actual,
                                          Type required,
                                          llvm::ArrayRef<unsigned> mismatches,
                                          ConstraintLocator *locator);

private:
  MutableArrayRef<unsigned> getMismatchesBuf() {
    return {getTrailingObjects<unsigned>(), NumMismatches};
  }
};

/// Detect situations where key path doesn't have capability required
/// by the context e.g. read-only vs. writable, or either root or value
/// types are incorrect e.g.
///
/// ```swift
/// struct S { let foo: Int }
/// let _: WritableKeyPath<S, Int> = \.foo
/// ```
///
/// Here context requires a writable key path but `foo` property is
/// read-only.
class KeyPathContextualMismatch final : public ContextualMismatch {
  KeyPathContextualMismatch(ConstraintSystem &cs, Type lhs, Type rhs,
                            ConstraintLocator *locator)
      : ContextualMismatch(cs, lhs, rhs, locator) {}

public:
  std::string getName() const override {
    return "fix key path contextual mismatch";
  }

  static KeyPathContextualMismatch *
  create(ConstraintSystem &cs, Type lhs, Type rhs, ConstraintLocator *locator);
};

/// Detect situations when argument of the @autoclosure parameter is itself
/// marked as @autoclosure and is not applied. Form a fix which suggests a
/// proper way to forward such arguments, e.g.:
///
/// ```swift
/// func foo(_ fn: @autoclosure () -> Int) {}
/// func bar(_ fn: @autoclosure () -> Int) {
///   foo(fn) // error - fn should be called
/// }
/// ```
class AutoClosureForwarding final : public ConstraintFix {
  AutoClosureForwarding(ConstraintSystem &cs, ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::AutoClosureForwarding, locator) {}

public:
  std::string getName() const override { return "fix @autoclosure forwarding"; }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static AutoClosureForwarding *create(ConstraintSystem &cs,
                                       ConstraintLocator *locator);
};

/// Allow invalid pointer conversions for autoclosure result types as if the
/// pointer type is a function parameter rather than an autoclosure result.
class AllowAutoClosurePointerConversion final : public ContextualMismatch {
  AllowAutoClosurePointerConversion(ConstraintSystem &cs, Type pointeeType,
                                    Type pointerType, ConstraintLocator *locator)
      : ContextualMismatch(cs, pointeeType, pointerType, locator) {}

public:
  std::string getName() const override {
    return "allow pointer conversion for autoclosure result type";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static AllowAutoClosurePointerConversion *create(ConstraintSystem &cs,
                                                   Type pointeeType,
                                                   Type pointerType,
                                                   ConstraintLocator *locator);
};

class RemoveUnwrap final : public ConstraintFix {
  Type BaseType;

  RemoveUnwrap(ConstraintSystem &cs, Type baseType, ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::RemoveUnwrap, locator), BaseType(baseType) {}

public:
  std::string getName() const override {
    return "remove unwrap operator `!` or `?`";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static RemoveUnwrap *create(ConstraintSystem &cs, Type baseType,
                              ConstraintLocator *locator);
};

class InsertExplicitCall final : public ConstraintFix {
  InsertExplicitCall(ConstraintSystem &cs, ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::InsertCall, locator) {}

public:
  std::string getName() const override {
    return "insert explicit `()` to make a call";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static InsertExplicitCall *create(ConstraintSystem &cs,
                                    ConstraintLocator *locator);
};

class UsePropertyWrapper final : public ConstraintFix {
  VarDecl *Wrapped;
  bool UsingStorageWrapper;
  Type Base;
  Type Wrapper;

  UsePropertyWrapper(ConstraintSystem &cs, VarDecl *wrapped,
                     bool usingStorageWrapper, Type base, Type wrapper,
                     ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::UsePropertyWrapper, locator),
        Wrapped(wrapped), UsingStorageWrapper(usingStorageWrapper), Base(base),
        Wrapper(wrapper) {}

public:
  std::string getName() const override {
    return "insert '$' or '_' to use property wrapper type instead of wrapped type";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static UsePropertyWrapper *create(ConstraintSystem &cs, VarDecl *wrapped,
                                    bool usingStorageWrapper, Type base,
                                    Type wrapper, ConstraintLocator *locator);
};

class UseWrappedValue final : public ConstraintFix {
  VarDecl *PropertyWrapper;
  Type Base;
  Type Wrapper;

  UseWrappedValue(ConstraintSystem &cs, VarDecl *propertyWrapper, Type base,
                  Type wrapper, ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::UseWrappedValue, locator),
        PropertyWrapper(propertyWrapper), Base(base), Wrapper(wrapper) {}

  bool usingStorageWrapper() const {
    auto nameStr = PropertyWrapper->getName().str();
    return !nameStr.startswith("_");
  }

public:
  std::string getName() const override {
    return "remove '$' or _ to use wrapped type instead of wrapper type";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static UseWrappedValue *create(ConstraintSystem &cs, VarDecl *propertyWrapper,
                                 Type base, Type wrapper,
                                 ConstraintLocator *locator);
};

class UseSubscriptOperator final : public ConstraintFix {
  UseSubscriptOperator(ConstraintSystem &cs, ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::UseSubscriptOperator, locator) {}

public:
  std::string getName() const override {
    return "replace '.subscript(...)' with subscript operator";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static UseSubscriptOperator *create(ConstraintSystem &cs,
                                      ConstraintLocator *locator);
};

class DefineMemberBasedOnUse final : public ConstraintFix {
  Type BaseType;
  DeclName Name;

  DefineMemberBasedOnUse(ConstraintSystem &cs, Type baseType, DeclName member,
                         ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::DefineMemberBasedOnUse, locator),
        BaseType(baseType), Name(member) {}

public:
  std::string getName() const override {
    llvm::SmallVector<char, 16> scratch;
    auto memberName = Name.getString(scratch);
    return "define missing member named '" + memberName.str() +
           "' based on its use";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static DefineMemberBasedOnUse *create(ConstraintSystem &cs, Type baseType,
                                        DeclName member,
                                        ConstraintLocator *locator);
};

class AllowInvalidMemberRef : public ConstraintFix {
  Type BaseType;
  ValueDecl *Member;
  DeclName Name;

protected:
  AllowInvalidMemberRef(ConstraintSystem &cs, FixKind kind, Type baseType,
                        ValueDecl *member, DeclName name,
                        ConstraintLocator *locator)
      : ConstraintFix(cs, kind, locator), BaseType(baseType), Member(member),
        Name(name) {}

public:
  Type getBaseType() const { return BaseType; }

  ValueDecl *getMember() const { return Member; }

  DeclName getMemberName() const { return Name; }
};

class AllowMemberRefOnExistential final : public AllowInvalidMemberRef {
  AllowMemberRefOnExistential(ConstraintSystem &cs, Type baseType,
                              DeclName memberName, ValueDecl *member,
                              ConstraintLocator *locator)
      : AllowInvalidMemberRef(cs, FixKind::AllowMemberRefOnExistential,
                              baseType, member, memberName, locator) {}

public:
  std::string getName() const override {
    llvm::SmallVector<char, 16> scratch;
    auto memberName = getMemberName().getString(scratch);
    return "allow access to invalid member '" + memberName.str() +
           "' on value of protocol type";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static AllowMemberRefOnExistential *create(ConstraintSystem &cs,
                                             Type baseType, ValueDecl *member,
                                             DeclName memberName,
                                             ConstraintLocator *locator);
};

class AllowTypeOrInstanceMember final : public AllowInvalidMemberRef {
  AllowTypeOrInstanceMember(ConstraintSystem &cs, Type baseType,
                            ValueDecl *member, DeclName name,
                            ConstraintLocator *locator)
      : AllowInvalidMemberRef(cs, FixKind::AllowTypeOrInstanceMember, baseType,
                              member, name, locator) {
    assert(member);
  }

public:
  std::string getName() const override {
    return "allow access to instance member on type or a type member on instance";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static AllowTypeOrInstanceMember *create(ConstraintSystem &cs, Type baseType,
                                           ValueDecl *member, DeclName usedName,
                                           ConstraintLocator *locator);
};

class AllowInvalidPartialApplication final : public ConstraintFix {
  AllowInvalidPartialApplication(bool isWarning, ConstraintSystem &cs,
                                 ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::AllowInvalidPartialApplication, locator,
                      isWarning) {}

public:
  std::string getName() const override {
    return "allow partially applied 'mutating' method";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static AllowInvalidPartialApplication *create(bool isWarning,
                                                ConstraintSystem &cs,
                                                ConstraintLocator *locator);
};

class AllowInvalidInitRef final : public ConstraintFix {
  enum class RefKind {
    DynamicOnMetatype,
    ProtocolMetatype,
    NonConstMetatype,
  } Kind;

  Type BaseType;
  const ConstructorDecl *Init;
  bool IsStaticallyDerived;
  SourceRange BaseRange;

  AllowInvalidInitRef(ConstraintSystem &cs, RefKind kind, Type baseTy,
                      ConstructorDecl *init, bool isStaticallyDerived,
                      SourceRange baseRange, ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::AllowInvalidInitRef, locator), Kind(kind),
        BaseType(baseTy), Init(init), IsStaticallyDerived(isStaticallyDerived),
        BaseRange(baseRange) {}

public:
  std::string getName() const override {
    return "allow invalid initializer reference";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static AllowInvalidInitRef *
  dynamicOnMetatype(ConstraintSystem &cs, Type baseTy, ConstructorDecl *init,
                    SourceRange baseRange, ConstraintLocator *locator);

  static AllowInvalidInitRef *
  onProtocolMetatype(ConstraintSystem &cs, Type baseTy, ConstructorDecl *init,
                     bool isStaticallyDerived, SourceRange baseRange,
                     ConstraintLocator *locator);

  static AllowInvalidInitRef *onNonConstMetatype(ConstraintSystem &cs,
                                                 Type baseTy,
                                                 ConstructorDecl *init,
                                                 ConstraintLocator *locator);

private:
  static AllowInvalidInitRef *create(RefKind kind, ConstraintSystem &cs,
                                     Type baseTy, ConstructorDecl *init,
                                     bool isStaticallyDerived,
                                     SourceRange baseRange,
                                     ConstraintLocator *locator);
};

class AllowTupleTypeMismatch final : public ContextualMismatch {
  AllowTupleTypeMismatch(ConstraintSystem &cs, Type lhs, Type rhs,
                         ConstraintLocator *locator)
      : ContextualMismatch(cs, FixKind::AllowTupleTypeMismatch, lhs, rhs,
                           locator) {}

public:
  static AllowTupleTypeMismatch *create(ConstraintSystem &cs, Type lhs,
                                        Type rhs, ConstraintLocator *locator);

  std::string getName() const override {
    return "fix tuple mismatches in type and arity";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;
};

class AllowMutatingMemberOnRValueBase final : public AllowInvalidMemberRef {
  AllowMutatingMemberOnRValueBase(ConstraintSystem &cs, Type baseType,
                                  ValueDecl *member, DeclName name,
                                  ConstraintLocator *locator)
      : AllowInvalidMemberRef(cs, FixKind::AllowMutatingMemberOnRValueBase,
                              baseType, member, name, locator) {}

public:
  std::string getName() const override {
    return "allow `mutating` method on r-value base";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static AllowMutatingMemberOnRValueBase *
  create(ConstraintSystem &cs, Type baseType, ValueDecl *member, DeclName name,
         ConstraintLocator *locator);
};

class AllowClosureParamDestructuring final : public ConstraintFix {
  FunctionType *ContextualType;

  AllowClosureParamDestructuring(ConstraintSystem &cs,
                                 FunctionType *contextualType,
                                 ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::AllowClosureParameterDestructuring, locator),
        ContextualType(contextualType) {}

public:
  std::string getName() const override {
    return "allow closure parameter destructuring";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static AllowClosureParamDestructuring *create(ConstraintSystem &cs,
                                                FunctionType *contextualType,
                                                ConstraintLocator *locator);
};

class AddMissingArguments final
    : public ConstraintFix,
      private llvm::TrailingObjects<AddMissingArguments,
                                    AnyFunctionType::Param> {
  friend TrailingObjects;

  using Param = AnyFunctionType::Param;

  unsigned NumSynthesized;

  AddMissingArguments(ConstraintSystem &cs,
                      llvm::ArrayRef<Param> synthesizedArgs,
                      ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::AddMissingArguments, locator),
        NumSynthesized(synthesizedArgs.size()) {
    std::uninitialized_copy(synthesizedArgs.begin(), synthesizedArgs.end(),
                            getSynthesizedArgumentsBuf().begin());
  }

public:
  std::string getName() const override { return "synthesize missing argument(s)"; }

  ArrayRef<Param> getSynthesizedArguments() const {
    return {getTrailingObjects<Param>(), NumSynthesized};
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static AddMissingArguments *create(ConstraintSystem &cs,
                                     llvm::ArrayRef<Param> synthesizedArgs,
                                     ConstraintLocator *locator);

private:
  MutableArrayRef<Param> getSynthesizedArgumentsBuf() {
    return {getTrailingObjects<Param>(), NumSynthesized};
  }
};

class MoveOutOfOrderArgument final : public ConstraintFix {
  using ParamBinding = SmallVector<unsigned, 1>;

  unsigned ArgIdx;
  unsigned PrevArgIdx;

  SmallVector<ParamBinding, 4> Bindings;

  MoveOutOfOrderArgument(ConstraintSystem &cs, unsigned argIdx,
                         unsigned prevArgIdx, ArrayRef<ParamBinding> bindings,
                         ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::MoveOutOfOrderArgument, locator),
        ArgIdx(argIdx), PrevArgIdx(prevArgIdx),
        Bindings(bindings.begin(), bindings.end()) {}

public:
  std::string getName() const override {
    return "move out-of-order argument to correct position";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static MoveOutOfOrderArgument *create(ConstraintSystem &cs,
                                        unsigned argIdx,
                                        unsigned prevArgIdx,
                                        ArrayRef<ParamBinding> bindings,
                                        ConstraintLocator *locator);
};

class AllowInaccessibleMember final : public AllowInvalidMemberRef {
  AllowInaccessibleMember(ConstraintSystem &cs, Type baseType,
                          ValueDecl *member, DeclName name,
                          ConstraintLocator *locator)
      : AllowInvalidMemberRef(cs, FixKind::AllowInaccessibleMember, baseType,
                              member, name, locator) {}

public:
  std::string getName() const override {
    return "allow inaccessible member reference";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static AllowInaccessibleMember *create(ConstraintSystem &cs, Type baseType,
                                         ValueDecl *member, DeclName name,
                                         ConstraintLocator *locator);
};

class AllowAnyObjectKeyPathRoot final : public ConstraintFix {

  AllowAnyObjectKeyPathRoot(ConstraintSystem &cs, ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::AllowAnyObjectKeyPathRoot, locator) {}

public:
  std::string getName() const override {
    return "allow anyobject as root type for a keypath";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static AllowAnyObjectKeyPathRoot *create(ConstraintSystem &cs,
                                           ConstraintLocator *locator);
};

class TreatKeyPathSubscriptIndexAsHashable final : public ConstraintFix {
  Type NonConformingType;

  TreatKeyPathSubscriptIndexAsHashable(ConstraintSystem &cs, Type type,
                                       ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::TreatKeyPathSubscriptIndexAsHashable,
                      locator),
        NonConformingType(type) {}

public:
  std::string getName() const override {
    return "treat keypath subscript index as conforming to Hashable";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static TreatKeyPathSubscriptIndexAsHashable *
  create(ConstraintSystem &cs, Type type, ConstraintLocator *locator);
};

class AllowInvalidRefInKeyPath final : public ConstraintFix {
  enum RefKind {
    // Allow a reference to a static member as a key path component.
    StaticMember,
    // Allow a reference to a declaration with mutating getter as
    // a key path component.
    MutatingGetter,
    // Allow a reference to a method (instance or static) as
    // a key path component.
    Method,
  } Kind;

  ValueDecl *Member;

  AllowInvalidRefInKeyPath(ConstraintSystem &cs, RefKind kind,
                           ValueDecl *member, ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::AllowInvalidRefInKeyPath, locator),
        Kind(kind), Member(member) {}

public:
  std::string getName() const override {
    switch (Kind) {
    case RefKind::StaticMember:
      return "allow reference to a static member as a key path component";
    case RefKind::MutatingGetter:
      return "allow reference to a member with mutating getter as a key "
             "path component";
    case RefKind::Method:
      return "allow reference to a method as a key path component";
    }
    llvm_unreachable("covered switch");
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  /// Determine whether give reference requires a fix and produce one.
  static AllowInvalidRefInKeyPath *
  forRef(ConstraintSystem &cs, ValueDecl *member, ConstraintLocator *locator);

private:
  static AllowInvalidRefInKeyPath *create(ConstraintSystem &cs, RefKind kind,
                                          ValueDecl *member,
                                          ConstraintLocator *locator);
};

class RemoveReturn final : public ConstraintFix {
  RemoveReturn(ConstraintSystem &cs, ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::RemoveReturn, locator) {}

public:
  std::string getName() const override { return "remove or omit return type"; }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static RemoveReturn *create(ConstraintSystem &cs, ConstraintLocator *locator);
};

class CollectionElementContextualMismatch final : public ContextualMismatch {
  CollectionElementContextualMismatch(ConstraintSystem &cs, Type srcType,
                                      Type dstType, ConstraintLocator *locator)
      : ContextualMismatch(cs, srcType, dstType, locator) {}

public:
  std::string getName() const override {
    return "fix collection element contextual mismatch";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static CollectionElementContextualMismatch *
  create(ConstraintSystem &cs, Type srcType, Type dstType,
         ConstraintLocator *locator);
};

class ExplicitlySpecifyGenericArguments final
    : public ConstraintFix,
      private llvm::TrailingObjects<ExplicitlySpecifyGenericArguments,
                                    GenericTypeParamType *> {
  friend TrailingObjects;

  unsigned NumMissingParams;

  ExplicitlySpecifyGenericArguments(ConstraintSystem &cs,
                                    ArrayRef<GenericTypeParamType *> params,
                                    ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::ExplicitlySpecifyGenericArguments, locator),
        NumMissingParams(params.size()) {
    assert(!params.empty());
    std::uninitialized_copy(params.begin(), params.end(),
                            getParametersBuf().begin());
  }

public:
  std::string getName() const override {
    return "default missing generic arguments to `Any`";
  }

  ArrayRef<GenericTypeParamType *> getParameters() const {
    return {getTrailingObjects<GenericTypeParamType *>(), NumMissingParams};
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static ExplicitlySpecifyGenericArguments *
  create(ConstraintSystem &cs, ArrayRef<GenericTypeParamType *> params,
         ConstraintLocator *locator);

private:
  MutableArrayRef<GenericTypeParamType *> getParametersBuf() {
    return {getTrailingObjects<GenericTypeParamType *>(), NumMissingParams};
  }
};

class SkipUnhandledConstructInFunctionBuilder final : public ConstraintFix {
public:
  using UnhandledNode = llvm::PointerUnion<Stmt *, Decl *>;

private:
  UnhandledNode unhandled;
  NominalTypeDecl *builder;

  SkipUnhandledConstructInFunctionBuilder(ConstraintSystem &cs,
                                          UnhandledNode unhandled,
                                          NominalTypeDecl *builder,
                                          ConstraintLocator *locator)
    : ConstraintFix(cs, FixKind::SkipUnhandledConstructInFunctionBuilder,
                    locator),
      unhandled(unhandled), builder(builder) { }

public:
  std::string getName() const override {
    return "skip unhandled constructs when applying a function builder";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static SkipUnhandledConstructInFunctionBuilder *
  create(ConstraintSystem &cs, UnhandledNode unhandledNode,
         NominalTypeDecl *builder, ConstraintLocator *locator);
};

class AllowTupleSplatForSingleParameter final : public ConstraintFix {
  using Param = AnyFunctionType::Param;

  Type ParamType;

  AllowTupleSplatForSingleParameter(ConstraintSystem &cs, Type paramTy,
                                    ConstraintLocator *locator)
      : ConstraintFix(cs, FixKind::AllowTupleSplatForSingleParameter, locator),
        ParamType(paramTy) {}

public:
  std::string getName() const override {
    return "allow single parameter tuple splat";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  /// Apply this fix to given arguments/parameters and return `true`
  /// this fix is not applicable and solver can't continue, `false`
  /// otherwise.
  static bool attempt(ConstraintSystem &cs, SmallVectorImpl<Param> &args,
                      ArrayRef<Param> params,
                      SmallVectorImpl<SmallVector<unsigned, 1>> &bindings,
                      ConstraintLocatorBuilder locator);
};

class IgnoreContextualType : public ContextualMismatch {
  IgnoreContextualType(ConstraintSystem &cs, Type resultTy, Type specifiedTy,
                       ConstraintLocator *locator)
      : ContextualMismatch(cs, resultTy, specifiedTy, locator) {}

public:
  std::string getName() const override {
    return "ignore specified contextual type";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static IgnoreContextualType *create(ConstraintSystem &cs, Type resultTy,
                                      Type specifiedTy,
                                      ConstraintLocator *locator);
};

class IgnoreAssignmentDestinationType final : public ContextualMismatch {
  IgnoreAssignmentDestinationType(ConstraintSystem &cs, Type sourceTy,
                                  Type destTy, ConstraintLocator *locator)
      : ContextualMismatch(cs, sourceTy, destTy, locator) {}

public:
  std::string getName() const override {
    return "ignore type of the assignment destination";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static IgnoreAssignmentDestinationType *create(ConstraintSystem &cs,
                                                 Type sourceTy, Type destTy,
                                                 ConstraintLocator *locator);
};

/// If this is an argument-to-parameter conversion which is associated with
/// `inout` parameter, subtyping is not permitted, types have to
/// be identical.
class AllowInOutConversion final : public ContextualMismatch {
  AllowInOutConversion(ConstraintSystem &cs, Type argType, Type paramType,
                       ConstraintLocator *locator)
      : ContextualMismatch(cs, argType, paramType, locator) {}

public:
  std::string getName() const override {
    return "allow conversions between argument/parameter marked as `inout`";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static AllowInOutConversion *create(ConstraintSystem &cs, Type argType,
                                      Type paramType,
                                      ConstraintLocator *locator);
};

class AllowArgumentMismatch : public ContextualMismatch {
protected:
  AllowArgumentMismatch(ConstraintSystem &cs, Type argType, Type paramType,
                        ConstraintLocator *locator)
      : AllowArgumentMismatch(cs, FixKind::AllowArgumentTypeMismatch, argType,
                              paramType, locator) {}

  AllowArgumentMismatch(ConstraintSystem &cs, FixKind kind, Type argType,
                        Type paramType, ConstraintLocator *locator,
                        bool warning = false)
      : ContextualMismatch(cs, kind, argType, paramType, locator, warning) {}

public:
  std::string getName() const override {
    return "allow argument to parameter type conversion mismatch";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static AllowArgumentMismatch *create(ConstraintSystem &cs, Type argType,
                                       Type paramType,
                                       ConstraintLocator *locator);
};

class ExpandArrayIntoVarargs final : public AllowArgumentMismatch {

  ExpandArrayIntoVarargs(ConstraintSystem &cs, Type argType, Type paramType,
                         ConstraintLocator *locator)
      : AllowArgumentMismatch(cs, FixKind::ExpandArrayIntoVarargs, argType,
                              paramType, locator) {}

public:
  std::string getName() const override {
    return "cannot pass Array elements as variadic arguments";
  }

  bool diagnose(Expr *root, bool asNote = false) const override;

  static ExpandArrayIntoVarargs *attempt(ConstraintSystem &cs, Type argType,
                                         Type paramType,
                                         ConstraintLocatorBuilder locator);
};

class ExplicitlyConstructRawRepresentable final : public AllowArgumentMismatch {
  ExplicitlyConstructRawRepresentable(ConstraintSystem &cs, Type argType,
                                      Type paramType,
                                      ConstraintLocator *locator)
      : AllowArgumentMismatch(cs, FixKind::ExplicitlyConstructRawRepresentable,
                              argType, paramType, locator) {}

public:
  std::string getName() const override {
    return "explicitly construct a raw representable type";
  }

  static ExplicitlyConstructRawRepresentable *
  attempt(ConstraintSystem &cs, Type argType, Type paramType,
          ConstraintLocatorBuilder locator);
};

class UseValueTypeOfRawRepresentative final : public AllowArgumentMismatch {
  UseValueTypeOfRawRepresentative(ConstraintSystem &cs, Type argType,
                                  Type paramType, ConstraintLocator *locator)
      : AllowArgumentMismatch(cs, FixKind::UseValueTypeOfRawRepresentative,
                              argType, paramType, locator) {}

public:
  std::string getName() const override {
    return "use `.rawValue` of a raw representable type";
  }

  static UseValueTypeOfRawRepresentative *
  attempt(ConstraintSystem &cs, Type argType, Type paramType,
          ConstraintLocatorBuilder locator);
};

/// Replace a coercion ('as') with a forced checked cast ('as!').
class CoerceToCheckedCast final : public ContextualMismatch {
  CoerceToCheckedCast(ConstraintSystem &cs, Type fromType, Type toType,
                      ConstraintLocator *locator)
      : ContextualMismatch(cs, FixKind::CoerceToCheckedCast, fromType, toType,
                           locator) {}

public:
  std::string getName() const { return "as to as!"; }

  bool diagnose(Expr *root, bool asNote = false) const;

  static CoerceToCheckedCast *attempt(ConstraintSystem &cs, Type fromType,
                                      Type toType, ConstraintLocator *locator);
};

} // end namespace constraints
} // end namespace swift

#endif // SWIFT_SEMA_CSFIX_H
