//===-- ClangDiagnosticsEmitter.cpp - Generate Clang diagnostics tables ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These tablegen backends emit Clang diagnostics tables.
//
//===----------------------------------------------------------------------===//

#include "TableGenBackends.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Format.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/StringToOffsetTable.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <set>
using namespace llvm;

//===----------------------------------------------------------------------===//
// Diagnostic category computation code.
//===----------------------------------------------------------------------===//

namespace {
class DiagGroupParentMap {
  const RecordKeeper &Records;
  std::map<const Record *, std::vector<const Record *>> Mapping;

public:
  DiagGroupParentMap(const RecordKeeper &records) : Records(records) {
    for (const Record *Group : Records.getAllDerivedDefinitions("DiagGroup"))
      for (const Record *SubGroup : Group->getValueAsListOfDefs("SubGroups"))
        Mapping[SubGroup].push_back(Group);
  }

  ArrayRef<const Record *> getParents(const Record *Group) {
    return Mapping[Group];
  }
};
} // end anonymous namespace.

static StringRef
getCategoryFromDiagGroup(const Record *Group,
                         DiagGroupParentMap &DiagGroupParents) {
  // If the DiagGroup has a category, return it.
  StringRef CatName = Group->getValueAsString("CategoryName");
  if (!CatName.empty()) return CatName;

  // The diag group may the subgroup of one or more other diagnostic groups,
  // check these for a category as well.
  for (const Record *Parent : DiagGroupParents.getParents(Group)) {
    CatName = getCategoryFromDiagGroup(Parent, DiagGroupParents);
    if (!CatName.empty()) return CatName;
  }
  return "";
}

/// getDiagnosticCategory - Return the category that the specified diagnostic
/// lives in.
static StringRef getDiagnosticCategory(const Record *R,
                                       DiagGroupParentMap &DiagGroupParents) {
  // If the diagnostic is in a group, and that group has a category, use it.
  if (const auto *Group = dyn_cast<DefInit>(R->getValueInit("Group"))) {
    // Check the diagnostic's diag group for a category.
    StringRef CatName =
        getCategoryFromDiagGroup(Group->getDef(), DiagGroupParents);
    if (!CatName.empty()) return CatName;
  }

  // If the diagnostic itself has a category, get it.
  return R->getValueAsString("CategoryName");
}

namespace {
  class DiagCategoryIDMap {
    const RecordKeeper &Records;
    StringMap<unsigned> CategoryIDs;
    std::vector<StringRef> CategoryStrings;

  public:
    DiagCategoryIDMap(const RecordKeeper &records) : Records(records) {
      DiagGroupParentMap ParentInfo(Records);

      // The zero'th category is "".
      CategoryStrings.push_back("");
      CategoryIDs[""] = 0;

      for (const Record *Diag :
           Records.getAllDerivedDefinitions("Diagnostic")) {
        StringRef Category = getDiagnosticCategory(Diag, ParentInfo);
        if (Category.empty()) continue;  // Skip diags with no category.

        unsigned &ID = CategoryIDs[Category];
        if (ID != 0) continue;  // Already seen.

        ID = CategoryStrings.size();
        CategoryStrings.push_back(Category);
      }
    }

    unsigned getID(StringRef CategoryString) {
      return CategoryIDs[CategoryString];
    }

    typedef std::vector<StringRef>::const_iterator const_iterator;
    const_iterator begin() const { return CategoryStrings.begin(); }
    const_iterator end() const { return CategoryStrings.end(); }
  };

  struct GroupInfo {
    StringRef GroupName;
    std::vector<const Record*> DiagsInGroup;
    std::vector<StringRef> SubGroups;
    unsigned IDNo = 0;

    SmallVector<const Record *, 1> Defs;

    GroupInfo() = default;
  };
} // end anonymous namespace.

static bool beforeThanCompare(const Record *LHS, const Record *RHS) {
  assert(!LHS->getLoc().empty() && !RHS->getLoc().empty());
  return
    LHS->getLoc().front().getPointer() < RHS->getLoc().front().getPointer();
}

static bool diagGroupBeforeByName(const Record *LHS, const Record *RHS) {
  return LHS->getValueAsString("GroupName") <
         RHS->getValueAsString("GroupName");
}

using DiagsInGroupTy = std::map<StringRef, GroupInfo>;

/// Invert the 1-[0/1] mapping of diags to group into a one to many
/// mapping of groups to diags in the group.
static void groupDiagnostics(ArrayRef<const Record *> Diags,
                             ArrayRef<const Record *> DiagGroups,
                             DiagsInGroupTy &DiagsInGroup) {
  for (const Record *R : Diags) {
    const auto *DI = dyn_cast<DefInit>(R->getValueInit("Group"));
    if (!DI)
      continue;
    assert(R->getValueAsDef("Class")->getName() != "CLASS_NOTE" &&
           "Note can't be in a DiagGroup");
    StringRef GroupName = DI->getDef()->getValueAsString("GroupName");
    DiagsInGroup[GroupName].DiagsInGroup.push_back(R);
  }

  // Add all DiagGroup's to the DiagsInGroup list to make sure we pick up empty
  // groups (these are warnings that GCC supports that clang never produces).
  for (const Record *Group : DiagGroups) {
    GroupInfo &GI = DiagsInGroup[Group->getValueAsString("GroupName")];
    GI.GroupName = Group->getName();
    GI.Defs.push_back(Group);

    for (const Record *SubGroup : Group->getValueAsListOfDefs("SubGroups"))
      GI.SubGroups.push_back(SubGroup->getValueAsString("GroupName"));
  }

  // Assign unique ID numbers to the groups.
  for (auto [IdNo, Iter] : enumerate(DiagsInGroup))
    Iter.second.IDNo = IdNo;

  // Warn if the same group is defined more than once (including implicitly).
  for (auto &Group : DiagsInGroup) {
    if (Group.second.Defs.size() == 1 &&
        (!Group.second.Defs.front()->isAnonymous() ||
         Group.second.DiagsInGroup.size() <= 1))
      continue;

    bool First = true;
    for (const Record *Def : Group.second.Defs) {
      // Skip implicit definitions from diagnostics; we'll report those
      // separately below.
      bool IsImplicit = false;
      for (const Record *Diag : Group.second.DiagsInGroup) {
        if (cast<DefInit>(Diag->getValueInit("Group"))->getDef() == Def) {
          IsImplicit = true;
          break;
        }
      }
      if (IsImplicit)
        continue;

      SMLoc Loc = Def->getLoc().front();
      if (First) {
        SrcMgr.PrintMessage(Loc, SourceMgr::DK_Error,
                            Twine("group '") + Group.first +
                                "' is defined more than once");
        First = false;
      } else {
        SrcMgr.PrintMessage(Loc, SourceMgr::DK_Note, "also defined here");
      }
    }

    for (const Record *Diag : Group.second.DiagsInGroup) {
      if (!cast<DefInit>(Diag->getValueInit("Group"))->getDef()->isAnonymous())
        continue;

      SMLoc Loc = Diag->getLoc().front();
      if (First) {
        SrcMgr.PrintMessage(Loc, SourceMgr::DK_Error,
                            Twine("group '") + Group.first +
                                "' is implicitly defined more than once");
        First = false;
      } else {
        SrcMgr.PrintMessage(Loc, SourceMgr::DK_Note,
                            "also implicitly defined here");
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// Infer members of -Wpedantic.
//===----------------------------------------------------------------------===//

typedef std::vector<const Record *> RecordVec;
typedef DenseSet<const Record *> RecordSet;
typedef PointerUnion<RecordVec *, RecordSet *> VecOrSet;

namespace {
class InferPedantic {
  typedef DenseMap<const Record *, std::pair<unsigned, std::optional<unsigned>>>
      GMap;

  DiagGroupParentMap &DiagGroupParents;
  ArrayRef<const Record *> Diags;
  const std::vector<const Record *> DiagGroups;
  DiagsInGroupTy &DiagsInGroup;
  DenseSet<const Record *> DiagsSet;
  GMap GroupCount;
public:
  InferPedantic(DiagGroupParentMap &DiagGroupParents,
                ArrayRef<const Record *> Diags,
                ArrayRef<const Record *> DiagGroups,
                DiagsInGroupTy &DiagsInGroup)
      : DiagGroupParents(DiagGroupParents), Diags(Diags),
        DiagGroups(DiagGroups), DiagsInGroup(DiagsInGroup) {}

  /// Compute the set of diagnostics and groups that are immediately
  /// in -Wpedantic.
  void compute(VecOrSet DiagsInPedantic,
               VecOrSet GroupsInPedantic);

private:
  /// Determine whether a group is a subgroup of another group.
  bool isSubGroupOfGroup(const Record *Group, StringRef RootGroupName);

  /// Determine if the diagnostic is an extension.
  bool isExtension(const Record *Diag);

  /// Determine if the diagnostic is off by default.
  bool isOffByDefault(const Record *Diag);

  /// Increment the count for a group, and transitively marked
  /// parent groups when appropriate.
  void markGroup(const Record *Group);

  /// Return true if the diagnostic is in a pedantic group.
  bool groupInPedantic(const Record *Group, bool increment = false);
};
} // end anonymous namespace

bool InferPedantic::isSubGroupOfGroup(const Record *Group, StringRef GName) {
  StringRef GroupName = Group->getValueAsString("GroupName");
  if (GName == GroupName)
    return true;

  for (const Record *Parent : DiagGroupParents.getParents(Group))
    if (isSubGroupOfGroup(Parent, GName))
      return true;

  return false;
}

/// Determine if the diagnostic is an extension.
bool InferPedantic::isExtension(const Record *Diag) {
  return Diag->getValueAsDef("Class")->getName() == "CLASS_EXTENSION";
}

bool InferPedantic::isOffByDefault(const Record *Diag) {
  return Diag->getValueAsDef("DefaultSeverity")->getValueAsString("Name") ==
         "Ignored";
}

bool InferPedantic::groupInPedantic(const Record *Group, bool increment) {
  GMap::mapped_type &V = GroupCount[Group];
  // Lazily compute the threshold value for the group count.
  if (!V.second) {
    const GroupInfo &GI = DiagsInGroup[Group->getValueAsString("GroupName")];
    V.second = GI.SubGroups.size() + GI.DiagsInGroup.size();
  }

  if (increment)
    ++V.first;

  // Consider a group in -Wpendatic IFF if has at least one diagnostic
  // or subgroup AND all of those diagnostics and subgroups are covered
  // by -Wpedantic via our computation.
  return V.first != 0 && V.first == *V.second;
}

void InferPedantic::markGroup(const Record *Group) {
  // If all the diagnostics and subgroups have been marked as being
  // covered by -Wpedantic, increment the count of parent groups.  Once the
  // group's count is equal to the number of subgroups and diagnostics in
  // that group, we can safely add this group to -Wpedantic.
  if (groupInPedantic(Group, /* increment */ true))
    for (const Record *Parent : DiagGroupParents.getParents(Group))
      markGroup(Parent);
}

void InferPedantic::compute(VecOrSet DiagsInPedantic,
                            VecOrSet GroupsInPedantic) {
  // All extensions that are not on by default are implicitly in the
  // "pedantic" group.  For those that aren't explicitly included in -Wpedantic,
  // mark them for consideration to be included in -Wpedantic directly.
  for (const Record *R : Diags) {
    if (!isExtension(R) || !isOffByDefault(R))
      continue;
    DiagsSet.insert(R);
    if (const auto *Group = dyn_cast<DefInit>(R->getValueInit("Group"))) {
      const Record *GroupRec = Group->getDef();
      if (!isSubGroupOfGroup(GroupRec, "pedantic")) {
        markGroup(GroupRec);
      }
    }
  }

  // Compute the set of diagnostics that are directly in -Wpedantic.  We
  // march through Diags a second time to ensure the results are emitted
  // in deterministic order.
  for (const Record *R : Diags) {
    if (!DiagsSet.count(R))
      continue;
    // Check if the group is implicitly in -Wpedantic.  If so,
    // the diagnostic should not be directly included in the -Wpedantic
    // diagnostic group.
    if (const auto *Group = dyn_cast<DefInit>(R->getValueInit("Group")))
      if (groupInPedantic(Group->getDef()))
        continue;

    // The diagnostic is not included in a group that is (transitively) in
    // -Wpedantic.  Include it in -Wpedantic directly.
    if (auto *V = dyn_cast<RecordVec *>(DiagsInPedantic))
      V->push_back(R);
    else
      cast<RecordSet *>(DiagsInPedantic)->insert(R);
  }

  if (!GroupsInPedantic)
    return;

  // Compute the set of groups that are directly in -Wpedantic.  We
  // march through the groups to ensure the results are emitted
  /// in a deterministc order.
  for (const Record *Group : DiagGroups) {
    if (!groupInPedantic(Group))
      continue;

    const std::vector<const Record *> &Parents =
        DiagGroupParents.getParents(Group);
    bool AllParentsInPedantic =
        all_of(Parents, [&](const Record *R) { return groupInPedantic(R); });
    // If all the parents are in -Wpedantic, this means that this diagnostic
    // group will be indirectly included by -Wpedantic already.  In that
    // case, do not add it directly to -Wpedantic.  If the group has no
    // parents, obviously it should go into -Wpedantic.
    if (Parents.size() > 0 && AllParentsInPedantic)
      continue;

    if (auto *V = dyn_cast<RecordVec *>(GroupsInPedantic))
      V->push_back(Group);
    else
      cast<RecordSet *>(GroupsInPedantic)->insert(Group);
  }
}

namespace {
enum PieceKind {
  MultiPieceClass,
  TextPieceClass,
  PlaceholderPieceClass,
  SelectPieceClass,
  EnumSelectPieceClass,
  PluralPieceClass,
  DiffPieceClass,
  SubstitutionPieceClass,
};

enum ModifierType {
  MT_Unknown,
  MT_Placeholder,
  MT_Select,
  MT_EnumSelect,
  MT_Sub,
  MT_Plural,
  MT_Diff,
  MT_Ordinal,
  MT_Human,
  MT_S,
  MT_Q,
  MT_ObjCClass,
  MT_ObjCInstance,
  MT_Quoted,
};

static StringRef getModifierName(ModifierType MT) {
  switch (MT) {
  case MT_EnumSelect:
  case MT_Select:
    return "select";
  case MT_Sub:
    return "sub";
  case MT_Diff:
    return "diff";
  case MT_Plural:
    return "plural";
  case MT_Ordinal:
    return "ordinal";
  case MT_Human:
    return "human";
  case MT_S:
    return "s";
  case MT_Q:
    return "q";
  case MT_Placeholder:
    return "";
  case MT_ObjCClass:
    return "objcclass";
  case MT_ObjCInstance:
    return "objcinstance";
  case MT_Quoted:
    return "quoted";
  case MT_Unknown:
    llvm_unreachable("invalid modifier type");
  }
  // Unhandled case
  llvm_unreachable("invalid modifier type");
}

struct Piece {
  // This type and its derived classes are move-only.
  Piece(PieceKind Kind) : ClassKind(Kind) {}
  Piece(Piece const &O) = delete;
  Piece &operator=(Piece const &) = delete;
  virtual ~Piece() {}

  PieceKind getPieceClass() const { return ClassKind; }
  static bool classof(const Piece *) { return true; }

private:
  PieceKind ClassKind;
};

struct MultiPiece : Piece {
  MultiPiece() : Piece(MultiPieceClass) {}
  MultiPiece(std::vector<Piece *> Pieces)
      : Piece(MultiPieceClass), Pieces(std::move(Pieces)) {}

  std::vector<Piece *> Pieces;

  static bool classof(const Piece *P) {
    return P->getPieceClass() == MultiPieceClass;
  }
};

struct TextPiece : Piece {
  StringRef Role;
  std::string Text;
  TextPiece(StringRef Text, StringRef Role = "")
      : Piece(TextPieceClass), Role(Role), Text(Text.str()) {}

  static bool classof(const Piece *P) {
    return P->getPieceClass() == TextPieceClass;
  }
};

struct PlaceholderPiece : Piece {
  ModifierType Kind;
  int Index;
  PlaceholderPiece(ModifierType Kind, int Index)
      : Piece(PlaceholderPieceClass), Kind(Kind), Index(Index) {}

  static bool classof(const Piece *P) {
    return P->getPieceClass() == PlaceholderPieceClass;
  }
};

struct SelectPiece : Piece {
protected:
  SelectPiece(PieceKind Kind, ModifierType ModKind)
      : Piece(Kind), ModKind(ModKind) {}

public:
  SelectPiece(ModifierType ModKind) : SelectPiece(SelectPieceClass, ModKind) {}

  ModifierType ModKind;
  std::vector<Piece *> Options;
  int Index = 0;

  static bool classof(const Piece *P) {
    return P->getPieceClass() == SelectPieceClass ||
           P->getPieceClass() == EnumSelectPieceClass ||
           P->getPieceClass() == PluralPieceClass;
  }
};

struct EnumSelectPiece : SelectPiece {
  EnumSelectPiece() : SelectPiece(EnumSelectPieceClass, MT_EnumSelect) {}

  StringRef EnumName;
  std::vector<StringRef> OptionEnumNames;

  static bool classof(const Piece *P) {
    return P->getPieceClass() == EnumSelectPieceClass;
  }
};

struct EnumValuePiece : Piece {
  ModifierType Kind;
};

struct PluralPiece : SelectPiece {
  PluralPiece() : SelectPiece(PluralPieceClass, MT_Plural) {}

  std::vector<Piece *> OptionPrefixes;
  int Index = 0;

  static bool classof(const Piece *P) {
    return P->getPieceClass() == PluralPieceClass;
  }
};

struct DiffPiece : Piece {
  DiffPiece() : Piece(DiffPieceClass) {}

  Piece *Parts[4] = {};
  int Indexes[2] = {};

  static bool classof(const Piece *P) {
    return P->getPieceClass() == DiffPieceClass;
  }
};

struct SubstitutionPiece : Piece {
  SubstitutionPiece() : Piece(SubstitutionPieceClass) {}

  std::string Name;
  std::vector<int> Modifiers;

  static bool classof(const Piece *P) {
    return P->getPieceClass() == SubstitutionPieceClass;
  }
};

/// Diagnostic text, parsed into pieces.


struct DiagnosticTextBuilder {
  DiagnosticTextBuilder(DiagnosticTextBuilder const &) = delete;
  DiagnosticTextBuilder &operator=(DiagnosticTextBuilder const &) = delete;

  DiagnosticTextBuilder(const RecordKeeper &Records) {
    // Build up the list of substitution records.
    for (auto *S : Records.getAllDerivedDefinitions("TextSubstitution")) {
      EvaluatingRecordGuard Guard(&EvaluatingRecord, S);
      Substitutions.try_emplace(
          S->getName(), DiagText(*this, S->getValueAsString("Substitution")));
    }

    // Check that no diagnostic definitions have the same name as a
    // substitution.
    for (const Record *Diag : Records.getAllDerivedDefinitions("Diagnostic")) {
      StringRef Name = Diag->getName();
      if (Substitutions.count(Name))
        llvm::PrintFatalError(
            Diag->getLoc(),
            "Diagnostic '" + Name +
                "' has same name as TextSubstitution definition");
    }
  }

  std::vector<std::string> buildForDocumentation(StringRef Role,
                                                 const Record *R);
  std::string buildForDefinition(const Record *R);
  llvm::SmallVector<std::pair<
      std::string, llvm::SmallVector<std::pair<unsigned, std::string>>>>
  buildForEnum(const Record *R);

  Piece *getSubstitution(SubstitutionPiece *S) const {
    auto It = Substitutions.find(S->Name);
    if (It == Substitutions.end())
      llvm::PrintFatalError("Failed to find substitution with name: " +
                            S->Name);
    return It->second.Root;
  }

  [[noreturn]] void PrintFatalError(Twine const &Msg) const {
    assert(EvaluatingRecord && "not evaluating a record?");
    llvm::PrintFatalError(EvaluatingRecord->getLoc(), Msg);
  }

private:
  struct DiagText {
    DiagnosticTextBuilder &Builder;
    std::vector<Piece *> AllocatedPieces;
    Piece *Root = nullptr;

    template <class T, class... Args> T *New(Args &&... args) {
      static_assert(std::is_base_of<Piece, T>::value, "must be piece");
      T *Mem = new T(std::forward<Args>(args)...);
      AllocatedPieces.push_back(Mem);
      return Mem;
    }

    DiagText(DiagnosticTextBuilder &Builder, StringRef Text)
        : Builder(Builder), Root(parseDiagText(Text, StopAt::End)) {}

    enum class StopAt {
      // Parse until the end of the string.
      End,
      // Additionally stop if we hit a non-nested '|' or '}'.
      PipeOrCloseBrace,
      // Additionally stop if we hit a non-nested '$'.
      Dollar,
    };

    Piece *parseDiagText(StringRef &Text, StopAt Stop);
    int parseModifier(StringRef &) const;

  public:
    DiagText(DiagText &&O) noexcept
        : Builder(O.Builder), AllocatedPieces(std::move(O.AllocatedPieces)),
          Root(O.Root) {
      O.Root = nullptr;
    }
    // The move assignment operator is defined as deleted pending further
    // motivation.
    DiagText &operator=(DiagText &&) = delete;

    // The copy constrcutor and copy assignment operator is defined as deleted
    // pending further motivation.
    DiagText(const DiagText &) = delete;
    DiagText &operator=(const DiagText &) = delete;

    ~DiagText() {
      for (Piece *P : AllocatedPieces)
        delete P;
    }
  };

private:
  const Record *EvaluatingRecord = nullptr;
  struct EvaluatingRecordGuard {
    EvaluatingRecordGuard(const Record **Dest, const Record *New)
        : Dest(Dest), Old(*Dest) {
      *Dest = New;
    }
    ~EvaluatingRecordGuard() { *Dest = Old; }
    const Record **Dest;
    const Record *Old;
  };

  StringMap<DiagText> Substitutions;
};

template <class Derived> struct DiagTextVisitor {
  using ModifierMappingsType = std::optional<std::vector<int>>;

private:
  Derived &getDerived() { return static_cast<Derived &>(*this); }

public:
  std::vector<int>
  getSubstitutionMappings(SubstitutionPiece *P,
                          const ModifierMappingsType &Mappings) const {
    std::vector<int> NewMappings;
    for (int Idx : P->Modifiers)
      NewMappings.push_back(mapIndex(Idx, Mappings));
    return NewMappings;
  }

  struct SubstitutionContext {
    SubstitutionContext(DiagTextVisitor &Visitor, SubstitutionPiece *P)
        : Visitor(Visitor) {
      Substitution = Visitor.Builder.getSubstitution(P);
      OldMappings = std::move(Visitor.ModifierMappings);
      std::vector<int> NewMappings =
          Visitor.getSubstitutionMappings(P, OldMappings);
      Visitor.ModifierMappings = std::move(NewMappings);
    }

    ~SubstitutionContext() {
      Visitor.ModifierMappings = std::move(OldMappings);
    }

  private:
    DiagTextVisitor &Visitor;
    std::optional<std::vector<int>> OldMappings;

  public:
    Piece *Substitution;
  };

public:
  DiagTextVisitor(DiagnosticTextBuilder &Builder) : Builder(Builder) {}

  void Visit(Piece *P) {
    switch (P->getPieceClass()) {
#define CASE(T)                                                                \
  case T##PieceClass:                                                          \
    return getDerived().Visit##T(static_cast<T##Piece *>(P))
      CASE(Multi);
      CASE(Text);
      CASE(Placeholder);
      CASE(Select);
      CASE(EnumSelect);
      CASE(Plural);
      CASE(Diff);
      CASE(Substitution);
#undef CASE
    }
  }

  void VisitSubstitution(SubstitutionPiece *P) {
    SubstitutionContext Guard(*this, P);
    Visit(Guard.Substitution);
  }

  int mapIndex(int Idx,
                    ModifierMappingsType const &ModifierMappings) const {
    if (!ModifierMappings)
      return Idx;
    if (ModifierMappings->size() <= static_cast<unsigned>(Idx))
      Builder.PrintFatalError("Modifier value '" + std::to_string(Idx) +
                              "' is not valid for this mapping (has " +
                              std::to_string(ModifierMappings->size()) +
                              " mappings)");
    return (*ModifierMappings)[Idx];
  }

  int mapIndex(int Idx) const {
    return mapIndex(Idx, ModifierMappings);
  }

protected:
  DiagnosticTextBuilder &Builder;
  ModifierMappingsType ModifierMappings;
};

void escapeRST(StringRef Str, std::string &Out) {
  for (auto K : Str) {
    if (StringRef("`*|_[]\\").count(K))
      Out.push_back('\\');
    Out.push_back(K);
  }
}

template <typename It> void padToSameLength(It Begin, It End) {
  size_t Width = 0;
  for (It I = Begin; I != End; ++I)
    Width = std::max(Width, I->size());
  for (It I = Begin; I != End; ++I)
    (*I) += std::string(Width - I->size(), ' ');
}

template <typename It> void makeTableRows(It Begin, It End) {
  if (Begin == End)
    return;
  padToSameLength(Begin, End);
  for (It I = Begin; I != End; ++I)
    *I = "|" + *I + "|";
}

void makeRowSeparator(std::string &Str) {
  for (char &K : Str)
    K = (K == '|' ? '+' : '-');
}

struct DiagTextDocPrinter : DiagTextVisitor<DiagTextDocPrinter> {
  using BaseTy = DiagTextVisitor<DiagTextDocPrinter>;
  DiagTextDocPrinter(DiagnosticTextBuilder &Builder,
                     std::vector<std::string> &RST)
      : BaseTy(Builder), RST(RST) {}

  void gatherNodes(
      Piece *OrigP, const ModifierMappingsType &CurrentMappings,
      std::vector<std::pair<Piece *, ModifierMappingsType>> &Pieces) const {
    if (auto *Sub = dyn_cast<SubstitutionPiece>(OrigP)) {
      ModifierMappingsType NewMappings =
          getSubstitutionMappings(Sub, CurrentMappings);
      return gatherNodes(Builder.getSubstitution(Sub), NewMappings, Pieces);
    }
    if (auto *MD = dyn_cast<MultiPiece>(OrigP)) {
      for (Piece *Node : MD->Pieces)
        gatherNodes(Node, CurrentMappings, Pieces);
      return;
    }
    Pieces.push_back(std::make_pair(OrigP, CurrentMappings));
  }

  void VisitMulti(MultiPiece *P) {
    if (P->Pieces.empty()) {
      RST.push_back("");
      return;
    }

    if (P->Pieces.size() == 1)
      return Visit(P->Pieces[0]);

    // Flatten the list of nodes, replacing any substitution pieces with the
    // recursively flattened substituted node.
    std::vector<std::pair<Piece *, ModifierMappingsType>> Pieces;
    gatherNodes(P, ModifierMappings, Pieces);

    std::string EmptyLinePrefix;
    size_t Start = RST.size();
    bool HasMultipleLines = true;
    for (const std::pair<Piece *, ModifierMappingsType> &NodePair : Pieces) {
      std::vector<std::string> Lines;
      DiagTextDocPrinter Visitor{Builder, Lines};
      Visitor.ModifierMappings = NodePair.second;
      Visitor.Visit(NodePair.first);

      if (Lines.empty())
        continue;

      // We need a vertical separator if either this or the previous piece is a
      // multi-line piece, or this is the last piece.
      const char *Separator = (Lines.size() > 1 || HasMultipleLines) ? "|" : "";
      HasMultipleLines = Lines.size() > 1;

      if (Start + Lines.size() > RST.size())
        RST.resize(Start + Lines.size(), EmptyLinePrefix);

      padToSameLength(Lines.begin(), Lines.end());
      for (size_t I = 0; I != Lines.size(); ++I)
        RST[Start + I] += Separator + Lines[I];
      std::string Empty(Lines[0].size(), ' ');
      for (size_t I = Start + Lines.size(); I != RST.size(); ++I)
        RST[I] += Separator + Empty;
      EmptyLinePrefix += Separator + Empty;
    }
    for (size_t I = Start; I != RST.size(); ++I)
      RST[I] += "|";
    EmptyLinePrefix += "|";

    makeRowSeparator(EmptyLinePrefix);
    RST.insert(RST.begin() + Start, EmptyLinePrefix);
    RST.insert(RST.end(), EmptyLinePrefix);
  }

  void VisitText(TextPiece *P) {
    RST.push_back("");
    auto &S = RST.back();

    StringRef T = P->Text;
    while (T.consume_front(" "))
      RST.back() += " |nbsp| ";

    std::string Suffix;
    while (T.consume_back(" "))
      Suffix += " |nbsp| ";

    if (!T.empty()) {
      S += ':';
      S += P->Role;
      S += ":`";
      escapeRST(T, S);
      S += '`';
    }

    S += Suffix;
  }

  void VisitPlaceholder(PlaceholderPiece *P) {
    RST.push_back(std::string(":placeholder:`") +
                  char('A' + mapIndex(P->Index)) + "`");
  }

  void VisitSelect(SelectPiece *P) {
    std::vector<size_t> SeparatorIndexes;
    SeparatorIndexes.push_back(RST.size());
    RST.emplace_back();
    for (auto *O : P->Options) {
      Visit(O);
      SeparatorIndexes.push_back(RST.size());
      RST.emplace_back();
    }

    makeTableRows(RST.begin() + SeparatorIndexes.front(),
                  RST.begin() + SeparatorIndexes.back() + 1);
    for (size_t I : SeparatorIndexes)
      makeRowSeparator(RST[I]);
  }

  void VisitEnumSelect(EnumSelectPiece *P) {
    // Document this as if it were a 'select', which properly prints all of the
    // options correctly in a readable/reasonable manner. There isn't really
    // anything valuable we could add to readers here.
    VisitSelect(P);
  }

  void VisitPlural(PluralPiece *P) { VisitSelect(P); }

  void VisitDiff(DiffPiece *P) {
    // Render %diff{a $ b $ c|d}e,f as %select{a %e b %f c|d}.
    PlaceholderPiece E(MT_Placeholder, P->Indexes[0]);
    PlaceholderPiece F(MT_Placeholder, P->Indexes[1]);

    MultiPiece FirstOption;
    FirstOption.Pieces.push_back(P->Parts[0]);
    FirstOption.Pieces.push_back(&E);
    FirstOption.Pieces.push_back(P->Parts[1]);
    FirstOption.Pieces.push_back(&F);
    FirstOption.Pieces.push_back(P->Parts[2]);

    SelectPiece Select(MT_Diff);
    Select.Options.push_back(&FirstOption);
    Select.Options.push_back(P->Parts[3]);

    VisitSelect(&Select);
  }

  std::vector<std::string> &RST;
};

struct DiagEnumPrinter : DiagTextVisitor<DiagEnumPrinter> {
public:
  using BaseTy = DiagTextVisitor<DiagEnumPrinter>;
  using EnumeratorItem = std::pair<unsigned, std::string>;
  using EnumeratorList = llvm::SmallVector<EnumeratorItem>;
  using ResultTy = llvm::SmallVector<std::pair<std::string, EnumeratorList>>;

  DiagEnumPrinter(DiagnosticTextBuilder &Builder, ResultTy &Result)
      : BaseTy(Builder), Result(Result) {}

  ResultTy &Result;

  void VisitMulti(MultiPiece *P) {
    for (auto *Child : P->Pieces)
      Visit(Child);
  }
  void VisitText(TextPiece *P) {}
  void VisitPlaceholder(PlaceholderPiece *P) {}
  void VisitDiff(DiffPiece *P) {}
  void VisitSelect(SelectPiece *P) {
    for (auto *D : P->Options)
      Visit(D);
  }
  void VisitPlural(PluralPiece *P) { VisitSelect(P); }
  void VisitEnumSelect(EnumSelectPiece *P) {
    assert(P->Options.size() == P->OptionEnumNames.size());

    if (!P->EnumName.empty()) {
      EnumeratorList List;

      for (const auto &Tup : llvm::enumerate(P->OptionEnumNames))
        if (!Tup.value().empty())
          List.emplace_back(Tup.index(), Tup.value());

      Result.emplace_back(P->EnumName, List);
    }

    VisitSelect(P);
  }
};

struct DiagTextPrinter : DiagTextVisitor<DiagTextPrinter> {
public:
  using BaseTy = DiagTextVisitor<DiagTextPrinter>;
  DiagTextPrinter(DiagnosticTextBuilder &Builder, std::string &Result)
      : BaseTy(Builder), Result(Result) {}

  void VisitMulti(MultiPiece *P) {
    for (auto *Child : P->Pieces)
      Visit(Child);
  }
  void VisitText(TextPiece *P) { Result += P->Text; }
  void VisitPlaceholder(PlaceholderPiece *P) {
    Result += "%";
    Result += getModifierName(P->Kind);
    addInt(mapIndex(P->Index));
  }
  void VisitSelect(SelectPiece *P) {
    Result += "%";
    Result += getModifierName(P->ModKind);
    if (P->ModKind == MT_Select || P->ModKind == MT_EnumSelect) {
      Result += "{";
      for (auto *D : P->Options) {
        Visit(D);
        Result += '|';
      }
      if (!P->Options.empty())
        Result.erase(--Result.end());
      Result += '}';
    }
    addInt(mapIndex(P->Index));
  }

  void VisitPlural(PluralPiece *P) {
    Result += "%plural{";
    assert(P->Options.size() == P->OptionPrefixes.size());
    for (const auto [Prefix, Option] :
         zip_equal(P->OptionPrefixes, P->Options)) {
      if (Prefix)
        Visit(Prefix);
      Visit(Option);
      Result += "|";
    }
    if (!P->Options.empty())
      Result.erase(--Result.end());
    Result += '}';
    addInt(mapIndex(P->Index));
  }

  void VisitEnumSelect(EnumSelectPiece *P) {
    // Print as if we are a 'select', which will result in the compiler just
    // treating this like a normal select.  This way we don't have to do any
    // special work for the compiler to consume these.
    VisitSelect(P);
  }

  void VisitDiff(DiffPiece *P) {
    Result += "%diff{";
    Visit(P->Parts[0]);
    Result += "$";
    Visit(P->Parts[1]);
    Result += "$";
    Visit(P->Parts[2]);
    Result += "|";
    Visit(P->Parts[3]);
    Result += "}";
    addInt(mapIndex(P->Indexes[0]));
    Result += ",";
    addInt(mapIndex(P->Indexes[1]));
  }

  void addInt(int Val) { Result += std::to_string(Val); }

  std::string &Result;
};

int DiagnosticTextBuilder::DiagText::parseModifier(StringRef &Text) const {
  if (Text.empty() || !isdigit(Text[0]))
    Builder.PrintFatalError("expected modifier in diagnostic");
  int Val = 0;
  do {
    Val *= 10;
    Val += Text[0] - '0';
    Text = Text.drop_front();
  } while (!Text.empty() && isdigit(Text[0]));
  return Val;
}

Piece *DiagnosticTextBuilder::DiagText::parseDiagText(StringRef &Text,
                                                      StopAt Stop) {
  std::vector<Piece *> Parsed;

  constexpr StringLiteral StopSets[] = {"%", "%|}", "%|}$"};
  StringRef StopSet = StopSets[static_cast<int>(Stop)];

  while (!Text.empty()) {
    size_t End = (size_t)-2;
    do
      End = Text.find_first_of(StopSet, End + 2);
    while (
        End < Text.size() - 1 && Text[End] == '%' &&
        (Text[End + 1] == '%' || Text[End + 1] == '|' || Text[End + 1] == '$'));

    if (End) {
      Parsed.push_back(New<TextPiece>(Text.slice(0, End), "diagtext"));
      Text = Text.substr(End);
      if (Text.empty())
        break;
    }

    if (Text[0] == '|' || Text[0] == '}' || Text[0] == '$')
      break;

    // Drop the '%'.
    Text = Text.drop_front();

    // Extract the (optional) modifier.
    size_t ModLength = Text.find_first_of("0123456789<{");
    StringRef Modifier = Text.slice(0, ModLength);
    Text = Text.substr(ModLength);
    ModifierType ModType = StringSwitch<ModifierType>{Modifier}
                               .Case("select", MT_Select)
                               .Case("enum_select", MT_EnumSelect)
                               .Case("sub", MT_Sub)
                               .Case("diff", MT_Diff)
                               .Case("plural", MT_Plural)
                               .Case("s", MT_S)
                               .Case("ordinal", MT_Ordinal)
                               .Case("human", MT_Human)
                               .Case("q", MT_Q)
                               .Case("objcclass", MT_ObjCClass)
                               .Case("objcinstance", MT_ObjCInstance)
                               .Case("quoted", MT_Quoted)
                               .Case("", MT_Placeholder)
                               .Default(MT_Unknown);

    auto ExpectAndConsume = [&](StringRef Prefix) {
      if (!Text.consume_front(Prefix))
        Builder.PrintFatalError("expected '" + Prefix + "' while parsing %" +
                                Modifier);
    };

    if (ModType != MT_EnumSelect && Text[0] == '<')
      Builder.PrintFatalError("modifier '<' syntax not valid with %" +
                              Modifier);

    switch (ModType) {
    case MT_Unknown:
      Builder.PrintFatalError("Unknown modifier type: " + Modifier);
    case MT_Select: {
      SelectPiece *Select = New<SelectPiece>(MT_Select);
      do {
        Text = Text.drop_front(); // '{' or '|'
        Select->Options.push_back(
            parseDiagText(Text, StopAt::PipeOrCloseBrace));
        assert(!Text.empty() && "malformed %select");
      } while (Text.front() == '|');
      ExpectAndConsume("}");
      Select->Index = parseModifier(Text);
      Parsed.push_back(Select);
      continue;
    }
    case MT_EnumSelect: {
      EnumSelectPiece *EnumSelect = New<EnumSelectPiece>();
      if (Text[0] != '<')
        Builder.PrintFatalError("expected '<' after " + Modifier);

      Text = Text.drop_front(); // Drop '<'
      size_t EnumNameLen = Text.find_first_of('>');
      EnumSelect->EnumName = Text.slice(0, EnumNameLen);
      Text = Text.substr(EnumNameLen);
      ExpectAndConsume(">");

      if (Text[0] != '{')
        Builder.PrintFatalError("expected '{' after " + Modifier);

      do {
        Text = Text.drop_front(); // '{' or '|'

        bool BracketsRequired = false;
        if (Text[0] == '%') {
          BracketsRequired = true;
          Text = Text.drop_front(); // '%'
          size_t OptionNameLen = Text.find_first_of("{");
          EnumSelect->OptionEnumNames.push_back(Text.slice(0, OptionNameLen));
          Text = Text.substr(OptionNameLen);
        } else {
          EnumSelect->OptionEnumNames.push_back({});
        }

        if (BracketsRequired)
          ExpectAndConsume("{");
        else if (Text.front() == '{') {
          Text = Text.drop_front();
          BracketsRequired = true;
        }

        EnumSelect->Options.push_back(
            parseDiagText(Text, StopAt::PipeOrCloseBrace));

        if (BracketsRequired)
          ExpectAndConsume("}");

        assert(!Text.empty() && "malformed %select");
      } while (Text.front() == '|');

      ExpectAndConsume("}");
      EnumSelect->Index = parseModifier(Text);
      Parsed.push_back(EnumSelect);
      continue;
    }
    case MT_Plural: {
      PluralPiece *Plural = New<PluralPiece>();
      do {
        Text = Text.drop_front(); // '{' or '|'
        size_t End = Text.find_first_of(':');
        if (End == StringRef::npos)
          Builder.PrintFatalError("expected ':' while parsing %plural");
        ++End;
        assert(!Text.empty());
        Plural->OptionPrefixes.push_back(
            New<TextPiece>(Text.slice(0, End), "diagtext"));
        Text = Text.substr(End);
        Plural->Options.push_back(
            parseDiagText(Text, StopAt::PipeOrCloseBrace));
        assert(!Text.empty() && "malformed %plural");
      } while (Text.front() == '|');
      ExpectAndConsume("}");
      Plural->Index = parseModifier(Text);
      Parsed.push_back(Plural);
      continue;
    }
    case MT_Sub: {
      SubstitutionPiece *Sub = New<SubstitutionPiece>();
      ExpectAndConsume("{");
      size_t NameSize = Text.find_first_of('}');
      assert(NameSize != size_t(-1) && "failed to find the end of the name");
      assert(NameSize != 0 && "empty name?");
      Sub->Name = Text.substr(0, NameSize).str();
      Text = Text.drop_front(NameSize);
      ExpectAndConsume("}");
      if (!Text.empty()) {
        while (true) {
          if (!isdigit(Text[0]))
            break;
          Sub->Modifiers.push_back(parseModifier(Text));
          if (!Text.consume_front(","))
            break;
          assert(!Text.empty() && isdigit(Text[0]) &&
                 "expected another modifier");
        }
      }
      Parsed.push_back(Sub);
      continue;
    }
    case MT_Diff: {
      DiffPiece *Diff = New<DiffPiece>();
      ExpectAndConsume("{");
      Diff->Parts[0] = parseDiagText(Text, StopAt::Dollar);
      ExpectAndConsume("$");
      Diff->Parts[1] = parseDiagText(Text, StopAt::Dollar);
      ExpectAndConsume("$");
      Diff->Parts[2] = parseDiagText(Text, StopAt::PipeOrCloseBrace);
      ExpectAndConsume("|");
      Diff->Parts[3] = parseDiagText(Text, StopAt::PipeOrCloseBrace);
      ExpectAndConsume("}");
      Diff->Indexes[0] = parseModifier(Text);
      ExpectAndConsume(",");
      Diff->Indexes[1] = parseModifier(Text);
      Parsed.push_back(Diff);
      continue;
    }
    case MT_S: {
      SelectPiece *Select = New<SelectPiece>(ModType);
      Select->Options.push_back(New<TextPiece>(""));
      Select->Options.push_back(New<TextPiece>("s", "diagtext"));
      Select->Index = parseModifier(Text);
      Parsed.push_back(Select);
      continue;
    }
    case MT_Q:
    case MT_Placeholder:
    case MT_ObjCClass:
    case MT_ObjCInstance:
    case MT_Quoted:
    case MT_Ordinal:
    case MT_Human: {
      Parsed.push_back(New<PlaceholderPiece>(ModType, parseModifier(Text)));
      continue;
    }
    }
  }

  return New<MultiPiece>(Parsed);
}

std::vector<std::string>
DiagnosticTextBuilder::buildForDocumentation(StringRef Severity,
                                             const Record *R) {
  EvaluatingRecordGuard Guard(&EvaluatingRecord, R);
  StringRef Text = R->getValueAsString("Summary");

  DiagText D(*this, Text);
  TextPiece *Prefix = D.New<TextPiece>(Severity, Severity);
  Prefix->Text += ": ";
  auto *MP = dyn_cast<MultiPiece>(D.Root);
  if (!MP) {
    MP = D.New<MultiPiece>();
    MP->Pieces.push_back(D.Root);
    D.Root = MP;
  }
  MP->Pieces.insert(MP->Pieces.begin(), Prefix);
  std::vector<std::string> Result;
  DiagTextDocPrinter{*this, Result}.Visit(D.Root);
  return Result;
}

DiagEnumPrinter::ResultTy DiagnosticTextBuilder::buildForEnum(const Record *R) {
  EvaluatingRecordGuard Guard(&EvaluatingRecord, R);
  StringRef Text = R->getValueAsString("Summary");
  DiagText D(*this, Text);
  DiagEnumPrinter::ResultTy Result;
  DiagEnumPrinter{*this, Result}.Visit(D.Root);
  return Result;
}

std::string DiagnosticTextBuilder::buildForDefinition(const Record *R) {
  EvaluatingRecordGuard Guard(&EvaluatingRecord, R);
  StringRef Text = R->getValueAsString("Summary");
  DiagText D(*this, Text);
  std::string Result;
  DiagTextPrinter{*this, Result}.Visit(D.Root);
  return Result;
}

} // namespace

//===----------------------------------------------------------------------===//
// Warning Tables (.inc file) generation.
//===----------------------------------------------------------------------===//

static bool isError(const Record &Diag) {
  return Diag.getValueAsDef("Class")->getName() == "CLASS_ERROR";
}

static bool isRemark(const Record &Diag) {
  return Diag.getValueAsDef("Class")->getName() == "CLASS_REMARK";
}

// Presumes the text has been split at the first whitespace or hyphen.
static bool isExemptAtStart(StringRef Text) {
  // Fast path, the first character is lowercase or not alphanumeric.
  if (Text.empty() || isLower(Text[0]) || !isAlnum(Text[0]))
    return true;

  // If the text is all uppercase (or numbers, +, or _), then we assume it's an
  // acronym and that's allowed. This covers cases like ISO, C23, C++14, and
  // OBJECT_MODE. However, if there's only a single letter other than "C", we
  // do not exempt it so that we catch a case like "A really bad idea" while
  // still allowing a case like "C does not allow...".
  if (all_of(Text, [](char C) {
        return isUpper(C) || isDigit(C) || C == '+' || C == '_';
      }))
    return Text.size() > 1 || Text[0] == 'C';

  // Otherwise, there are a few other exemptions.
  return StringSwitch<bool>(Text)
      .Case("AddressSanitizer", true)
      .Case("CFString", true)
      .Case("Clang", true)
      .Case("Fuchsia", true)
      .Case("GNUstep", true)
      .Case("IBOutletCollection", true)
      .Case("Microsoft", true)
      .Case("Neon", true)
      .StartsWith("NSInvocation", true) // NSInvocation, NSInvocation's
      .Case("Objective", true) // Objective-C (hyphen is a word boundary)
      .Case("OpenACC", true)
      .Case("OpenCL", true)
      .Case("OpenMP", true)
      .Case("Pascal", true)
      .Case("Swift", true)
      .Case("Unicode", true)
      .Case("Vulkan", true)
      .Case("WebAssembly", true)
      .Default(false);
}

// Does not presume the text has been split at all.
static bool isExemptAtEnd(StringRef Text) {
  // Rather than come up with a list of characters that are allowed, we go the
  // other way and look only for characters that are not allowed.
  switch (Text.back()) {
  default:
    return true;
  case '?':
    // Explicitly allowed to support "; did you mean?".
    return true;
  case '.':
  case '!':
    return false;
  }
}

static void verifyDiagnosticWording(const Record &Diag) {
  StringRef FullDiagText = Diag.getValueAsString("Summary");

  auto DiagnoseStart = [&](StringRef Text) {
    // Verify that the text does not start with a capital letter, except for
    // special cases that are exempt like ISO and C++. Find the first word
    // by looking for a word breaking character.
    char Separators[] = {' ', '-', ',', '}'};
    auto Iter = std::find_first_of(
        Text.begin(), Text.end(), std::begin(Separators), std::end(Separators));

    StringRef First = Text.substr(0, Iter - Text.begin());
    if (!isExemptAtStart(First)) {
      PrintError(&Diag,
                 "Diagnostics should not start with a capital letter; '" +
                     First + "' is invalid");
    }
  };

  auto DiagnoseEnd = [&](StringRef Text) {
    // Verify that the text does not end with punctuation like '.' or '!'.
    if (!isExemptAtEnd(Text)) {
      PrintError(&Diag, "Diagnostics should not end with punctuation; '" +
                            Text.substr(Text.size() - 1, 1) + "' is invalid");
    }
  };

  // If the diagnostic starts with %select, look through it to see whether any
  // of the options will cause a problem.
  if (FullDiagText.starts_with("%select{")) {
    // Do a balanced delimiter scan from the start of the text to find the
    // closing '}', skipping intermediary {} pairs.

    size_t BraceCount = 1;
    constexpr size_t PercentSelectBraceLen = sizeof("%select{") - 1;
    auto Iter = FullDiagText.begin() + PercentSelectBraceLen;
    for (auto End = FullDiagText.end(); Iter != End; ++Iter) {
      char Ch = *Iter;
      if (Ch == '{')
        ++BraceCount;
      else if (Ch == '}')
        --BraceCount;
      if (!BraceCount)
        break;
    }
    // Defending against a malformed diagnostic string.
    if (BraceCount != 0)
      return;

    StringRef SelectText =
        FullDiagText.substr(PercentSelectBraceLen, Iter - FullDiagText.begin() -
                                                       PercentSelectBraceLen);
    SmallVector<StringRef, 4> SelectPieces;
    SelectText.split(SelectPieces, '|');

    // Walk over all of the individual pieces of select text to see if any of
    // them start with an invalid character. If any of the select pieces is
    // empty, we need to look at the first word after the %select to see
    // whether that is invalid or not. If all of the pieces are fine, then we
    // don't need to check anything else about the start of the diagnostic.
    bool CheckSecondWord = false;
    for (StringRef Piece : SelectPieces) {
      if (Piece.empty())
        CheckSecondWord = true;
      else
        DiagnoseStart(Piece);
    }

    if (CheckSecondWord) {
      // There was an empty select piece, so we need to check the second
      // word. This catches situations like '%select{|fine}0 Not okay'. Add
      // two to account for the closing curly brace and the number after it.
      StringRef AfterSelect =
          FullDiagText.substr(Iter - FullDiagText.begin() + 2).ltrim();
      DiagnoseStart(AfterSelect);
    }
  } else {
    // If the start of the diagnostic is not %select, we can check the first
    // word and be done with it.
    DiagnoseStart(FullDiagText);
  }

  // If the last character in the diagnostic is a number preceded by a }, scan
  // backwards to see if this is for a %select{...}0. If it is, we need to look
  // at each piece to see whether it ends in punctuation or not.
  bool StillNeedToDiagEnd = true;
  if (isDigit(FullDiagText.back()) && *(FullDiagText.end() - 2) == '}') {
    // Scan backwards to find the opening curly brace.
    size_t BraceCount = 1;
    auto Iter = FullDiagText.end() - sizeof("}0");
    for (auto End = FullDiagText.begin(); Iter != End; --Iter) {
      char Ch = *Iter;
      if (Ch == '}')
        ++BraceCount;
      else if (Ch == '{')
        --BraceCount;
      if (!BraceCount)
        break;
    }
    // Defending against a malformed diagnostic string.
    if (BraceCount != 0)
      return;

    // Continue the backwards scan to find the word before the '{' to see if it
    // is 'select'.
    constexpr size_t SelectLen = sizeof("select") - 1;
    bool IsSelect =
        (FullDiagText.substr(Iter - SelectLen - FullDiagText.begin(),
                             SelectLen) == "select");
    if (IsSelect) {
      // Gather the content between the {} for the select in question so we can
      // split it into pieces.
      StillNeedToDiagEnd = false; // No longer need to handle the end.
      StringRef SelectText =
          FullDiagText.substr(Iter - FullDiagText.begin() + /*{*/ 1,
                              FullDiagText.end() - Iter - /*pos before }0*/ 3);
      SmallVector<StringRef, 4> SelectPieces;
      SelectText.split(SelectPieces, '|');
      for (StringRef Piece : SelectPieces) {
        // Not worrying about a situation like: "this is bar. %select{foo|}0".
        if (!Piece.empty())
          DiagnoseEnd(Piece);
      }
    }
  }

  // If we didn't already cover the diagnostic because of a %select, handle it
  // now.
  if (StillNeedToDiagEnd)
    DiagnoseEnd(FullDiagText);

  // FIXME: This could also be improved by looking for instances of clang or
  // gcc in the diagnostic and recommend Clang or GCC instead. However, this
  // runs into odd situations like [[clang::warn_unused_result]],
  // #pragma clang, or --unwindlib=libgcc.
}

/// ClangDiagsCompatIDsEmitter - Emit a set of 'compatibility diagnostic ids'
/// that map to a set of 2 regular diagnostic ids each and which are used to
/// simplify emitting compatibility warnings.
void clang::EmitClangDiagsCompatIDs(const llvm::RecordKeeper &Records,
                                    llvm::raw_ostream &OS,
                                    const std::string &Component) {
  ArrayRef<const Record *> Ids =
      Records.getAllDerivedDefinitions("CompatWarningId");

  StringRef PrevComponent = "";
  for (auto [I, R] : enumerate(make_pointee_range(Ids))) {
    StringRef DiagComponent = R.getValueAsString("Component");
    if (!Component.empty() && Component != DiagComponent)
      continue;

    StringRef CompatDiagName = R.getValueAsString("Name");
    StringRef Diag = R.getValueAsString("Diag");
    StringRef DiagPre = R.getValueAsString("DiagPre");
    int64_t CXXStdVer = R.getValueAsInt("Std");

    // We don't want to create empty enums since some compilers (including
    // Clang) warn about that, so these macros are used to avoid having to
    // unconditionally write 'enum {' and '};' in the headers.
    if (PrevComponent != DiagComponent) {
      if (!PrevComponent.empty())
        OS << "DIAG_COMPAT_IDS_END()\n";
      OS << "DIAG_COMPAT_IDS_BEGIN()\n";
      PrevComponent = DiagComponent;
    }

    // FIXME: We sometimes define multiple compat diagnostics with the same
    // name, e.g. 'constexpr_body_invalid_stmt' exists for C++14/20/23. It would
    // be nice if we could combine all of them into a single compatibility diag
    // id.
    OS << "DIAG_COMPAT_ID(" << I << ",";
    OS << CompatDiagName << "," << CXXStdVer << "," << Diag << "," << DiagPre;
    OS << ")\n";
  }

  if (!PrevComponent.empty())
    OS << "DIAG_COMPAT_IDS_END()\n";
}

/// ClangDiagsIntefaceEmitter - Emit the diagnostics interface header for
/// a Clang component.
void clang::EmitClangDiagsInterface(llvm::raw_ostream &OS,
                                    const std::string &Component) {
  if (Component.empty())
    PrintFatalError("'-gen-clang-diags-iface' requires a component name");

  std::string ComponentUpper = StringRef(Component).upper();
  const char *Comp = Component.c_str();
  const char *Upper = ComponentUpper.c_str();

  OS << llvm::format(R"c++(
namespace clang {
namespace diag {
enum {
#define DIAG(ENUM, FLAGS, DEFAULT_MAPPING, DESC, GROUP, SFINAE, NOWERROR,      \
             SHOWINSYSHEADER, SHOWINSYSMACRO, DEFERRABLE, CATEGORY)            \
  ENUM,
#define %sSTART
#include "clang/Basic/Diagnostic%sKinds.inc"
#undef DIAG
  NUM_BUILTIN_%s_DIAGNOSTICS
};

#define DIAG_ENUM(ENUM_NAME)                                                   \
  namespace ENUM_NAME {                                                        \
  enum {
#define DIAG_ENUM_ITEM(IDX, NAME) NAME = IDX,
#define DIAG_ENUM_END()                                                        \
  }                                                                            \
  ;                                                                            \
  }
#include "clang/Basic/Diagnostic%sEnums.inc"
#undef DIAG_ENUM_END
#undef DIAG_ENUM_ITEM
#undef DIAG_ENUM
} // end namespace diag

namespace diag_compat {
#define DIAG_COMPAT_IDS_BEGIN() enum {
#define DIAG_COMPAT_IDS_END()                                                  \
  }                                                                            \
  ;
#define DIAG_COMPAT_ID(IDX, NAME, ...) NAME = IDX,
#include "clang/Basic/Diagnostic%sCompatIDs.inc"
#undef DIAG_COMPAT_ID
#undef DIAG_COMPAT_IDS_BEGIN
#undef DIAG_COMPAT_IDS_END
} // end namespace diag_compat
} // end namespace clang
)c++",
                     Upper, Comp, Upper, Comp, Comp);
}

/// ClangDiagsEnumsEmitter - The top-level class emits .def files containing
/// declarations of Clang diagnostic enums for selects.
void clang::EmitClangDiagsEnums(const RecordKeeper &Records, raw_ostream &OS,
                                const std::string &Component) {
  DiagnosticTextBuilder DiagTextBuilder(Records);
  ArrayRef<const Record *> Diags =
      Records.getAllDerivedDefinitions("Diagnostic");

  llvm::SmallVector<std::pair<const Record *, std::string>> EnumerationNames;

  for (const Record &R : make_pointee_range(Diags)) {
    DiagEnumPrinter::ResultTy Enums = DiagTextBuilder.buildForEnum(&R);

    for (auto &Enumeration : Enums) {
      bool ShouldPrint =
          Component.empty() || Component == R.getValueAsString("Component");

      auto PreviousByName = llvm::find_if(EnumerationNames, [&](auto &Prev) {
        return Prev.second == Enumeration.first;
      });

      if (PreviousByName != EnumerationNames.end()) {
        PrintError(&R,
                   "Duplicate enumeration name '" + Enumeration.first + "'");
        PrintNote(PreviousByName->first->getLoc(),
                  "Previous diagnostic is here");
      }

      EnumerationNames.emplace_back(&R, Enumeration.first);

      if (ShouldPrint)
        OS << "DIAG_ENUM(" << Enumeration.first << ")\n";

      llvm::SmallVector<std::string> EnumeratorNames;
      for (auto &Enumerator : Enumeration.second) {
        if (llvm::is_contained(EnumeratorNames, Enumerator.second))
          PrintError(&R,
                     "Duplicate enumerator name '" + Enumerator.second + "'");
        EnumeratorNames.push_back(Enumerator.second);

        if (ShouldPrint)
          OS << "DIAG_ENUM_ITEM(" << Enumerator.first << ", "
             << Enumerator.second << ")\n";
      }
      if (ShouldPrint)
        OS << "DIAG_ENUM_END()\n";
    }
  }
}

/// ClangDiagsDefsEmitter - The top-level class emits .def files containing
/// declarations of Clang diagnostics.
void clang::EmitClangDiagsDefs(const RecordKeeper &Records, raw_ostream &OS,
                               const std::string &Component) {
  // Write the #if guard
  if (!Component.empty()) {
    std::string ComponentName = StringRef(Component).upper();
    OS << "#ifdef " << ComponentName << "START\n";
    OS << "__" << ComponentName << "START = DIAG_START_" << ComponentName
       << ",\n";
    OS << "#undef " << ComponentName << "START\n";
    OS << "#endif\n\n";
  }

  DiagnosticTextBuilder DiagTextBuilder(Records);

  ArrayRef<const Record *> Diags =
      Records.getAllDerivedDefinitions("Diagnostic");

  ArrayRef<const Record *> DiagGroups =
      Records.getAllDerivedDefinitions("DiagGroup");

  DiagsInGroupTy DiagsInGroup;
  groupDiagnostics(Diags, DiagGroups, DiagsInGroup);

  DiagCategoryIDMap CategoryIDs(Records);
  DiagGroupParentMap DGParentMap(Records);

  // Compute the set of diagnostics that are in -Wpedantic.
  RecordSet DiagsInPedantic;
  InferPedantic inferPedantic(DGParentMap, Diags, DiagGroups, DiagsInGroup);
  inferPedantic.compute(&DiagsInPedantic, (RecordVec*)nullptr);

  for (const Record &R : make_pointee_range(Diags)) {
    // Check if this is an error that is accidentally in a warning
    // group.
    if (isError(R)) {
      if (const auto *Group = dyn_cast<DefInit>(R.getValueInit("Group"))) {
        const Record *GroupRec = Group->getDef();
        StringRef GroupName = GroupRec->getValueAsString("GroupName");
        PrintFatalError(R.getLoc(), "Error " + R.getName() +
                      " cannot be in a warning group [" + GroupName + "]");
      }
    }

    // Check that all remarks have an associated diagnostic group.
    if (isRemark(R)) {
      if (!isa<DefInit>(R.getValueInit("Group"))) {
        PrintFatalError(R.getLoc(), "Error " + R.getName() +
                                        " not in any diagnostic group");
      }
    }

    // Filter by component.
    if (!Component.empty() && Component != R.getValueAsString("Component"))
      continue;

    // Validate diagnostic wording for common issues.
    verifyDiagnosticWording(R);

    OS << "DIAG(" << R.getName() << ", ";
    OS << R.getValueAsDef("Class")->getName();
    OS << ", (unsigned)diag::Severity::"
       << R.getValueAsDef("DefaultSeverity")->getValueAsString("Name");

    // Description string.
    OS << ", \"";
    OS.write_escaped(DiagTextBuilder.buildForDefinition(&R)) << '"';

    // Warning group associated with the diagnostic. This is stored as an index
    // into the alphabetically sorted warning group table.
    if (const auto *DI = dyn_cast<DefInit>(R.getValueInit("Group"))) {
      auto I = DiagsInGroup.find(DI->getDef()->getValueAsString("GroupName"));
      assert(I != DiagsInGroup.end());
      OS << ", " << I->second.IDNo;
    } else if (DiagsInPedantic.count(&R)) {
      auto I = DiagsInGroup.find("pedantic");
      assert(I != DiagsInGroup.end() && "pedantic group not defined");
      OS << ", " << I->second.IDNo;
    } else {
      OS << ", 0";
    }

    // SFINAE response.
    OS << ", " << R.getValueAsDef("SFINAE")->getName();

    // Default warning has no Werror bit.
    if (R.getValueAsBit("WarningNoWerror"))
      OS << ", true";
    else
      OS << ", false";

    if (R.getValueAsBit("ShowInSystemHeader"))
      OS << ", true";
    else
      OS << ", false";

    if (R.getValueAsBit("ShowInSystemMacro"))
      OS << ", true";
    else
      OS << ", false";

    if (R.getValueAsBit("Deferrable"))
      OS << ", true";
    else
      OS << ", false";

    // Category number.
    OS << ", " << CategoryIDs.getID(getDiagnosticCategory(&R, DGParentMap));
    OS << ")\n";
  }
}

//===----------------------------------------------------------------------===//
// Warning Group Tables generation
//===----------------------------------------------------------------------===//

static std::string getDiagCategoryEnum(StringRef name) {
  if (name.empty())
    return "DiagCat_None";
  SmallString<256> enumName = StringRef("DiagCat_");
  for (char C : name)
    enumName += isalnum(C) ? C : '_';
  return std::string(enumName);
}

/// Emit the array of diagnostic subgroups.
///
/// The array of diagnostic subgroups contains for each group a list of its
/// subgroups. The individual lists are separated by '-1'. Groups with no
/// subgroups are skipped.
///
/// \code
///   static const int16_t DiagSubGroups[] = {
///     /* Empty */ -1,
///     /* DiagSubGroup0 */ 142, -1,
///     /* DiagSubGroup13 */ 265, 322, 399, -1
///   }
/// \endcode
///
static void emitDiagSubGroups(DiagsInGroupTy &DiagsInGroup,
                              RecordVec &GroupsInPedantic, raw_ostream &OS) {
  OS << "static const int16_t DiagSubGroups[] = {\n"
     << "  /* Empty */ -1,\n";
  for (auto const &[Name, Group] : DiagsInGroup) {
    const bool IsPedantic = Name == "pedantic";
    const std::vector<StringRef> &SubGroups = Group.SubGroups;
    if (!SubGroups.empty() || (IsPedantic && !GroupsInPedantic.empty())) {
      OS << "  /* DiagSubGroup" << Group.IDNo << " */ ";
      for (StringRef SubGroup : SubGroups) {
        auto RI = DiagsInGroup.find(SubGroup);
        assert(RI != DiagsInGroup.end() && "Referenced without existing?");
        OS << RI->second.IDNo << ", ";
      }
      // Emit the groups implicitly in "pedantic".
      if (IsPedantic) {
        for (auto const &Group : GroupsInPedantic) {
          StringRef GroupName = Group->getValueAsString("GroupName");
          auto RI = DiagsInGroup.find(GroupName);
          assert(RI != DiagsInGroup.end() && "Referenced without existing?");
          OS << RI->second.IDNo << ", ";
        }
      }

      OS << "-1,\n";
    }
  }
  OS << "};\n\n";
}

/// Emit the list of diagnostic arrays.
///
/// This data structure is a large array that contains itself arrays of varying
/// size. Each array represents a list of diagnostics. The different arrays are
/// separated by the value '-1'.
///
/// \code
///   static const int16_t DiagArrays[] = {
///     /* Empty */ -1,
///     /* DiagArray1 */ diag::warn_pragma_message,
///                      -1,
///     /* DiagArray2 */ diag::warn_abs_too_small,
///                      diag::warn_unsigned_abs,
///                      diag::warn_wrong_absolute_value_type,
///                      -1
///   };
/// \endcode
///
static void emitDiagArrays(DiagsInGroupTy &DiagsInGroup,
                           RecordVec &DiagsInPedantic, raw_ostream &OS) {
  OS << "static const int16_t DiagArrays[] = {\n"
     << "  /* Empty */ -1,\n";
  for (const auto &[Name, Group] : DiagsInGroup) {
    const bool IsPedantic = Name == "pedantic";

    const std::vector<const Record *> &V = Group.DiagsInGroup;
    if (!V.empty() || (IsPedantic && !DiagsInPedantic.empty())) {
      OS << "  /* DiagArray" << Group.IDNo << " */ ";
      for (auto *Record : V)
        OS << "diag::" << Record->getName() << ", ";
      // Emit the diagnostics implicitly in "pedantic".
      if (IsPedantic) {
        for (auto const &Diag : DiagsInPedantic)
          OS << "diag::" << Diag->getName() << ", ";
      }
      OS << "-1,\n";
    }
  }
  OS << "};\n\n";
}

/// Emit a list of group names.
///
/// This creates an `llvm::StringTable` of all the diagnostic group names.
static void emitDiagGroupNames(const StringToOffsetTable &GroupNames,
                               raw_ostream &OS) {
  GroupNames.EmitStringTableDef(OS, "DiagGroupNames");
  OS << "\n";
}

/// Emit diagnostic arrays and related data structures.
///
/// This creates the actual diagnostic array, an array of diagnostic subgroups
/// and an array of subgroup names.
///
/// \code
///  #ifdef GET_DIAG_ARRAYS
///     static const int16_t DiagArrays[];
///     static const int16_t DiagSubGroups[];
///     static constexpr llvm::StringTable DiagGroupNames;
///  #endif
///  \endcode
static void emitAllDiagArrays(DiagsInGroupTy &DiagsInGroup,
                              RecordVec &DiagsInPedantic,
                              RecordVec &GroupsInPedantic,
                              const StringToOffsetTable &GroupNames,
                              raw_ostream &OS) {
  OS << "\n#ifdef GET_DIAG_ARRAYS\n";
  emitDiagArrays(DiagsInGroup, DiagsInPedantic, OS);
  emitDiagSubGroups(DiagsInGroup, GroupsInPedantic, OS);
  emitDiagGroupNames(GroupNames, OS);
  OS << "#endif // GET_DIAG_ARRAYS\n\n";
}

/// Emit diagnostic table.
///
/// The table is sorted by the name of the diagnostic group. Each element
/// consists of the name of the diagnostic group (given as offset in the
/// group name table), a reference to a list of diagnostics (optional) and a
/// reference to a set of subgroups (optional).
///
/// \code
/// #ifdef GET_DIAG_TABLE
///  {/* abi */              159, /* DiagArray11 */ 19, /* Empty */          0},
///  {/* aggregate-return */ 180, /* Empty */        0, /* Empty */          0},
///  {/* all */              197, /* Empty */        0, /* DiagSubGroup13 */ 3},
///  {/* deprecated */       1981,/* DiagArray1 */ 348, /* DiagSubGroup3 */  9},
/// #endif
/// \endcode
static void emitDiagTable(DiagsInGroupTy &DiagsInGroup,
                          RecordVec &DiagsInPedantic,
                          RecordVec &GroupsInPedantic,
                          const StringToOffsetTable &GroupNames,
                          raw_ostream &OS) {
  unsigned MaxLen = 0;

  for (auto const &I: DiagsInGroup)
    MaxLen = std::max(MaxLen, (unsigned)I.first.size());

  OS << "\n#ifdef DIAG_ENTRY\n";
  unsigned SubGroupIndex = 1, DiagArrayIndex = 1;
  for (auto const &[Name, GroupInfo] : DiagsInGroup) {
    // Group option string.
    OS << "DIAG_ENTRY(";
    OS << GroupInfo.GroupName << " /* ";

    if (Name.find_first_not_of("abcdefghijklmnopqrstuvwxyz"
                               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                               "0123456789!@#$%^*-+=:?") != std::string::npos)
      PrintFatalError("Invalid character in diagnostic group '" + Name + "'");
    OS << Name << " */, ";
    OS << *GroupNames.GetStringOffset(Name) << ", ";

    // Special handling for 'pedantic'.
    const bool IsPedantic = Name == "pedantic";

    // Diagnostics in the group.
    const std::vector<const Record *> &V = GroupInfo.DiagsInGroup;
    const bool hasDiags =
        !V.empty() || (IsPedantic && !DiagsInPedantic.empty());
    if (hasDiags) {
      OS << "/* DiagArray" << GroupInfo.IDNo << " */ " << DiagArrayIndex
         << ", ";
      if (IsPedantic)
        DiagArrayIndex += DiagsInPedantic.size();
      DiagArrayIndex += V.size() + 1;
    } else {
      OS << "0, ";
    }

    // Subgroups.
    const std::vector<StringRef> &SubGroups = GroupInfo.SubGroups;
    const bool hasSubGroups =
        !SubGroups.empty() || (IsPedantic && !GroupsInPedantic.empty());
    if (hasSubGroups) {
      OS << "/* DiagSubGroup" << GroupInfo.IDNo << " */ " << SubGroupIndex
         << ", ";
      if (IsPedantic)
        SubGroupIndex += GroupsInPedantic.size();
      SubGroupIndex += SubGroups.size() + 1;
    } else {
      OS << "0, ";
    }

    std::string Documentation = GroupInfo.Defs.back()
                                    ->getValue("Documentation")
                                    ->getValue()
                                    ->getAsUnquotedString();

    OS << "R\"(" << StringRef(Documentation).trim() << ")\"";

    OS << ")\n";
  }
  OS << "#endif // DIAG_ENTRY\n\n";
}

/// Emit the table of diagnostic categories.
///
/// The table has the form of macro calls that have two parameters. The
/// category's name as well as an enum that represents the category. The
/// table can be used by defining the macro 'CATEGORY' and including this
/// table right after.
///
/// \code
/// #ifdef GET_CATEGORY_TABLE
///   CATEGORY("Semantic Issue", DiagCat_Semantic_Issue)
///   CATEGORY("Lambda Issue", DiagCat_Lambda_Issue)
/// #endif
/// \endcode
static void emitCategoryTable(const RecordKeeper &Records, raw_ostream &OS) {
  DiagCategoryIDMap CategoriesByID(Records);
  OS << "\n#ifdef GET_CATEGORY_TABLE\n";
  for (auto const &C : CategoriesByID)
    OS << "CATEGORY(\"" << C << "\", " << getDiagCategoryEnum(C) << ")\n";
  OS << "#endif // GET_CATEGORY_TABLE\n\n";
}

void clang::EmitClangDiagGroups(const RecordKeeper &Records, raw_ostream &OS) {
  // Compute a mapping from a DiagGroup to all of its parents.
  DiagGroupParentMap DGParentMap(Records);

  ArrayRef<const Record *> Diags =
      Records.getAllDerivedDefinitions("Diagnostic");

  ArrayRef<const Record *> DiagGroups =
      Records.getAllDerivedDefinitions("DiagGroup");

  DiagsInGroupTy DiagsInGroup;
  groupDiagnostics(Diags, DiagGroups, DiagsInGroup);

  // All extensions are implicitly in the "pedantic" group.  Record the
  // implicit set of groups in the "pedantic" group, and use this information
  // later when emitting the group information for Pedantic.
  RecordVec DiagsInPedantic;
  RecordVec GroupsInPedantic;
  InferPedantic inferPedantic(DGParentMap, Diags, DiagGroups, DiagsInGroup);
  inferPedantic.compute(&DiagsInPedantic, &GroupsInPedantic);

  StringToOffsetTable GroupNames;
  for (const auto &[Name, Group] : DiagsInGroup) {
    GroupNames.GetOrAddStringOffset(Name);
  }

  emitAllDiagArrays(DiagsInGroup, DiagsInPedantic, GroupsInPedantic, GroupNames,
                    OS);
  emitDiagTable(DiagsInGroup, DiagsInPedantic, GroupsInPedantic, GroupNames,
                OS);
  emitCategoryTable(Records, OS);
}

//===----------------------------------------------------------------------===//
// Diagnostic name index generation
//===----------------------------------------------------------------------===//

void clang::EmitClangDiagsIndexName(const RecordKeeper &Records,
                                    raw_ostream &OS) {
  std::vector<const Record *> Diags =
      Records.getAllDerivedDefinitions("Diagnostic");

  sort(Diags, [](const Record *LHS, const Record *RHS) {
    return LHS->getName() < RHS->getName();
  });

  for (const Record *Elem : Diags)
    OS << "DIAG_NAME_INDEX(" << Elem->getName() << ")\n";
}

//===----------------------------------------------------------------------===//
// Diagnostic documentation generation
//===----------------------------------------------------------------------===//

namespace docs {
namespace {

bool isRemarkGroup(const Record *DiagGroup,
                   const DiagsInGroupTy &DiagsInGroup) {
  bool AnyRemarks = false, AnyNonRemarks = false;

  std::function<void(StringRef)> Visit = [&](StringRef GroupName) {
    auto &GroupInfo = DiagsInGroup.find(GroupName)->second;
    for (const Record *Diag : GroupInfo.DiagsInGroup)
      (isRemark(*Diag) ? AnyRemarks : AnyNonRemarks) = true;
    for (StringRef Name : GroupInfo.SubGroups)
      Visit(Name);
  };
  Visit(DiagGroup->getValueAsString("GroupName"));

  if (AnyRemarks && AnyNonRemarks)
    PrintFatalError(
        DiagGroup->getLoc(),
        "Diagnostic group contains both remark and non-remark diagnostics");
  return AnyRemarks;
}

std::string getDefaultSeverity(const Record *Diag) {
  return std::string(
      Diag->getValueAsDef("DefaultSeverity")->getValueAsString("Name"));
}

std::set<std::string> getDefaultSeverities(const Record *DiagGroup,
                                           const DiagsInGroupTy &DiagsInGroup) {
  std::set<std::string> States;

  std::function<void(StringRef)> Visit = [&](StringRef GroupName) {
    auto &GroupInfo = DiagsInGroup.find(GroupName)->second;
    for (const Record *Diag : GroupInfo.DiagsInGroup)
      States.insert(getDefaultSeverity(Diag));
    for (const auto &Name : GroupInfo.SubGroups)
      Visit(Name);
  };
  Visit(DiagGroup->getValueAsString("GroupName"));
  return States;
}

void writeHeader(StringRef Str, raw_ostream &OS, char Kind = '-') {
  OS << Str << "\n" << std::string(Str.size(), Kind) << "\n";
}

void writeDiagnosticText(DiagnosticTextBuilder &Builder, const Record *R,
                         StringRef Role, raw_ostream &OS) {
  StringRef Text = R->getValueAsString("Summary");
  if (Text == "%0")
    OS << "The text of this diagnostic is not controlled by Clang.\n\n";
  else {
    std::vector<std::string> Out = Builder.buildForDocumentation(Role, R);
    for (auto &Line : Out)
      OS << Line << "\n";
    OS << "\n";
  }
}

}  // namespace
}  // namespace docs

void clang::EmitClangDiagDocs(const RecordKeeper &Records, raw_ostream &OS) {
  using namespace docs;

  // Get the documentation introduction paragraph.
  const Record *Documentation = Records.getDef("GlobalDocumentation");
  if (!Documentation) {
    PrintFatalError("The Documentation top-level definition is missing, "
                    "no documentation will be generated.");
    return;
  }

  OS << Documentation->getValueAsString("Intro") << "\n";

  DiagnosticTextBuilder Builder(Records);

  ArrayRef<const Record *> Diags =
      Records.getAllDerivedDefinitions("Diagnostic");

  std::vector<const Record *> DiagGroups =
      Records.getAllDerivedDefinitions("DiagGroup");
  sort(DiagGroups, diagGroupBeforeByName);

  DiagGroupParentMap DGParentMap(Records);

  DiagsInGroupTy DiagsInGroup;
  groupDiagnostics(Diags, DiagGroups, DiagsInGroup);

  // Compute the set of diagnostics that are in -Wpedantic.
  {
    RecordSet DiagsInPedanticSet;
    RecordSet GroupsInPedanticSet;
    InferPedantic inferPedantic(DGParentMap, Diags, DiagGroups, DiagsInGroup);
    inferPedantic.compute(&DiagsInPedanticSet, &GroupsInPedanticSet);
    auto &PedDiags = DiagsInGroup["pedantic"];
    // Put the diagnostics into a deterministic order.
    RecordVec DiagsInPedantic(DiagsInPedanticSet.begin(),
                              DiagsInPedanticSet.end());
    RecordVec GroupsInPedantic(GroupsInPedanticSet.begin(),
                               GroupsInPedanticSet.end());
    sort(DiagsInPedantic, beforeThanCompare);
    sort(GroupsInPedantic, beforeThanCompare);
    PedDiags.DiagsInGroup.insert(PedDiags.DiagsInGroup.end(),
                                 DiagsInPedantic.begin(),
                                 DiagsInPedantic.end());
    for (auto *Group : GroupsInPedantic)
      PedDiags.SubGroups.push_back(Group->getValueAsString("GroupName"));
  }

  // FIXME: Write diagnostic categories and link to diagnostic groups in each.

  // Write out the diagnostic groups.
  for (const Record *G : DiagGroups) {
    bool IsRemarkGroup = isRemarkGroup(G, DiagsInGroup);
    auto &GroupInfo = DiagsInGroup[G->getValueAsString("GroupName")];
    bool IsSynonym = GroupInfo.DiagsInGroup.empty() &&
                     GroupInfo.SubGroups.size() == 1;

    writeHeader(((IsRemarkGroup ? "-R" : "-W") +
                    G->getValueAsString("GroupName")).str(),
                OS);

    if (!IsSynonym) {
      // FIXME: Ideally, all the diagnostics in a group should have the same
      // default state, but that is not currently the case.
      auto DefaultSeverities = getDefaultSeverities(G, DiagsInGroup);
      if (!DefaultSeverities.empty() && !DefaultSeverities.count("Ignored")) {
        bool AnyNonErrors = DefaultSeverities.count("Warning") ||
                            DefaultSeverities.count("Remark");
        if (!AnyNonErrors)
          OS << "This diagnostic is an error by default, but the flag ``-Wno-"
             << G->getValueAsString("GroupName") << "`` can be used to disable "
             << "the error.\n\n";
        else
          OS << "This diagnostic is enabled by default.\n\n";
      } else if (DefaultSeverities.size() > 1) {
        OS << "Some of the diagnostics controlled by this flag are enabled "
           << "by default.\n\n";
      }
    }

    if (!GroupInfo.SubGroups.empty()) {
      if (IsSynonym)
        OS << "Synonym for ";
      else if (GroupInfo.DiagsInGroup.empty())
        OS << "Controls ";
      else
        OS << "Also controls ";

      sort(GroupInfo.SubGroups);
      ListSeparator LS;
      for (StringRef Name : GroupInfo.SubGroups)
        OS << LS << "`" << (IsRemarkGroup ? "-R" : "-W") << Name << "`_";
      OS << ".\n\n";
    }

    if (!GroupInfo.DiagsInGroup.empty()) {
      OS << "**Diagnostic text:**\n\n";
      for (const Record *D : GroupInfo.DiagsInGroup) {
        auto Severity = getDefaultSeverity(D);
        Severity[0] = tolower(Severity[0]);
        if (Severity == "ignored")
          Severity = IsRemarkGroup ? "remark" : "warning";

        writeDiagnosticText(Builder, D, Severity, OS);
      }
    }

    auto Doc = G->getValueAsString("Documentation");
    if (!Doc.empty())
      OS << Doc;
    else if (GroupInfo.SubGroups.empty() && GroupInfo.DiagsInGroup.empty())
      OS << "This diagnostic flag exists for GCC compatibility, and has no "
            "effect in Clang.\n";
    OS << "\n";
  }
}
