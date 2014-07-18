//
// TypeRenameTransform.cpp: Rename C/C++/Objective-C/Objective-C++ Types
//

//
// Limitations:
// * No Objective-C category rename
// * No Y, Z part for @implementation X (C) : Y <Z> -- perhaps you shouldn't

#include "Transforms.h"
#include "RenameTransforms.h"
#include <clang/AST/DeclFriend.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/TypeLoc.h>

using namespace clang;

class TypeRenameTransform : public RenameTransform {
public:
  virtual void HandleTranslationUnit(ASTContext &C);
  
protected:
  void collectRenameDecls(DeclContext *DC, bool topLevel = false);
  void processDeclContext(DeclContext *DC, bool topLevel = false);  
  void processStmt(Stmt *S);
  
  // forceRewriteMacro is needed to handle expressions like VAArgExpr
  // TODO: be smart, if TL is not within a marco, it's do-able
  void processFunctionDecl(FunctionDecl *D);
  void processTypeLoc(TypeLoc TL, bool forceRewriteMacro = true);
  
  void processQualifierLoc(NestedNameSpecifierLoc NNSL,
                           bool forceRewriteMacro = false);
  void processParmVarDecl(ParmVarDecl *P);
  bool tagNameMatches(TagDecl *T);
  
private:
  // a quick way to get the whole TypeLocClass tree
  static std::string typeLocClassName(TypeLoc::TypeLocClass C) {
    std::string src;
    llvm::raw_string_ostream sst(src);

// will be undefined by clang/AST/TypeNodes.def
#define ABSTRACT_TYPE(Class, Base)
#define TYPE(Class, Base) case TypeLoc::Class: \
    sst << #Class; \
    break;

    switch(C) {
      #include <clang/AST/TypeNodes.def>
      case TypeLoc::Qualified:
        sst << "Qualified";
        break;
    }
    
    sst << "(" << C << ")";
    
    return sst.str();
  }
};

REGISTER_TRANSFORM(TypeRenameTransform);


void TypeRenameTransform::HandleTranslationUnit(ASTContext &C)
{
  auto I = loadConfig("TypeRename", "Types");
  if (!I) {
    return;
  }

  auto TUD = C.getTranslationUnitDecl();
  collectRenameDecls(TUD, true);
  processDeclContext(TUD, true);
}

void TypeRenameTransform::collectRenameDecls(DeclContext *DC, bool topLevel)
{
  // TODO: Skip globally touched locations
  //
  // if a.cpp and b.cpp both include c.h, then once a.cpp is processed,
  // we cas skip any location that is not in b.cpp
  //
  
  pushIndent();
  
  for(auto I = DC->decls_begin(), E = DC->decls_end(); I != E; ++I) {
    auto L = (*I)->getLocation();
    
    if (auto TD = dyn_cast<TagDecl>(*I)) {
      std::string newName;
      if (nameMatches(TD, newName)) {
        renameLocation(L, newName);
      }
    }
    else if (auto D = dyn_cast<ObjCContainerDecl>(*I)) {
      // Objective-C containers (@interface, @implementation incl. categories,
      // @protocol)
      std::string newName;
      if (nameMatches(D, newName)) {
        renameLocation(L, newName);
      }      
    }
    else if (auto D = dyn_cast<TypedefDecl>(*I)) {
      // typedef T n -- we want to see first if it's n that needs renaming
      std::string newName;
      if (nameMatches(D, newName)) {
        renameLocation(L, newName);
      }
    }
    else if (auto D = dyn_cast<ClassTemplateDecl>(*I)) {
      auto TD = D->getTemplatedDecl();
      if (auto RD = dyn_cast<CXXRecordDecl>(TD)) {
        std::string newName;
        if (nameMatches(RD, newName)) {
          renameLocation(L, newName);
        }
        
        collectRenameDecls(RD);
      }
    }

    // descend into the next level (namespace, etc.)    
    if (auto innerDC = dyn_cast<DeclContext>(*I)) {
      collectRenameDecls(innerDC);
    }
  }
  popIndent();  
}

void TypeRenameTransform::processDeclContext(DeclContext *DC, bool topLevel)
{  
  // TODO: Skip globally touched locations
  //
  // if a.cpp and b.cpp both include c.h, then once a.cpp is processed,
  // we cas skip any location that is not in b.cpp
  //

  pushIndent();
  
  for(auto I = DC->decls_begin(), E = DC->decls_end(); I != E; ++I) {
    auto L = (*I)->getLocation();
    if (topLevel && shouldIgnore(L)) {
      continue;
    }
    
    // llvm::errs() << indent() << (*I)->getDeclKindName() << ", at: " << loc(L) << "\n";

    if (auto D = dyn_cast<ClassTemplateDecl>(*I)) {
      auto TD = D->getTemplatedDecl();
      if (auto RD = dyn_cast<CXXRecordDecl>(TD)) {
        // we need to descend on our own      
        processDeclContext(RD);
      }
    }
    else if (auto D = dyn_cast<ClassTemplateSpecializationDecl>(*I)) {
      if (auto TSI = D->getTypeAsWritten()) {
        processTypeLoc(TSI->getTypeLoc());
      }
    }
    else if (auto TD = dyn_cast<TagDecl>(*I)) {
      if (auto CRD = dyn_cast<CXXRecordDecl>(TD)) {
        // can't call bases_begin() if there's no definition
        if (CRD->hasDefinition()) {        
          for (auto BI = CRD->bases_begin(), BE = CRD->bases_end();
               BI != BE; ++BI) {
            if (auto TSI = BI->getTypeSourceInfo()) {
              processTypeLoc(TSI->getTypeLoc());
            }
          }
          
          for (auto FI = CRD->friend_begin(), FE = CRD->friend_end();
               FI != FE; ++FI) {
            if (auto TSI = (*FI)->getFriendType()) {
              processTypeLoc(TSI->getTypeLoc());
            }            
          }
        }
      } // if a CXXRecordDecl
    }
    else if (auto D = dyn_cast<FunctionTemplateDecl>(*I)) {
      processFunctionDecl(D->getTemplatedDecl());
      
      // we need to descend on our own
      processDeclContext(D->getTemplatedDecl());
    }    
    else if (auto D = dyn_cast<FunctionDecl>(*I)) {
      processFunctionDecl(D);
    }
    else if (auto D = dyn_cast<VarDecl>(*I)) {
      if (auto TSI = D->getTypeSourceInfo()) {
        processTypeLoc(TSI->getTypeLoc());
      }
      
      processQualifierLoc(D->getQualifierLoc());      
      
      if (D->hasInit()) {
        processStmt(D->getInit());
      }
    }
    else if (auto D = dyn_cast<FieldDecl>(*I)) {
      if (auto TSI = D->getTypeSourceInfo()) {
        processTypeLoc(TSI->getTypeLoc());
      }
      
      processQualifierLoc(D->getQualifierLoc());      
    }
    else if (auto D = dyn_cast<TypedefDecl>(*I)) {
      // typedef T n, handle the case of T
      if (auto TSI = D->getTypeSourceInfo()) {
        processTypeLoc(TSI->getTypeLoc());
      }
    }
    else if (auto D = dyn_cast<ObjCMethodDecl>(*I)) {
      // if no type source info, it's a void f(void) function
      auto TSI = D->getResultTypeSourceInfo();
      if (TSI) {      
        processTypeLoc(TSI->getTypeLoc());
      }

      for (auto PI = D->param_begin(), PE = D->param_end(); PI != PE; ++PI) {        
        processParmVarDecl(*PI);
      }
      
      // handle body
      if (auto B = D->getBody()) {
        if (stmtInSameFileAsDecl(B, D)) {
          processStmt(B);
        }
      }
    }
    else if (auto D = dyn_cast<ObjCPropertyDecl>(*I)) {
      if (auto TSI = D->getTypeSourceInfo()) {
        processTypeLoc(TSI->getTypeLoc());
      }
    }
    
#define FIX_PROTOCOL(D) do { \
    std::string newName; \
    auto PLI = D->protocol_loc_begin(); \
    auto PLE = D->protocol_loc_end(); \
    for (auto I = D->protocol_begin(), E = D->protocol_end(); I != E; ++I) { \
      if (PLI == PLE) { \
        break; \
      } \
      if (nameMatches(*I, newName, true)) { \
        renameLocation(*PLI, newName); \
      } \
      ++PLI; \
    } \
  } while(0)
  
    else if (auto D = dyn_cast<ObjCCategoryDecl>(*I)) {
      // fix class name
      std::string newName;
      if (nameMatches(D->getClassInterface(), newName, true)) {
        renameLocation(D->getLocation(), newName);
      }
            
      // fix protocols
      FIX_PROTOCOL(D);
    }
    else if (auto D = dyn_cast<ObjCInterfaceDecl>(*I)) {
      // fix super class name
      auto SC = D->getSuperClass();
      std::string newName;
      if (nameMatches(SC, newName, true)) {
        renameLocation(D->getSuperClassLoc(), newName);
      }
     
      // fix protocols
      FIX_PROTOCOL(D);      
    }
    else if (auto D = dyn_cast<ObjCProtocolDecl>(*I)) {
      // fix protocols
      FIX_PROTOCOL(D);      
    }
    else if (auto D = dyn_cast<ObjCImplDecl>(*I)) {
      // fix class name
      std::string newName;
      if (nameMatches(D->getClassInterface(), newName, true)) {
        renameLocation(D->getLocation(), newName);
      }      
    }

    // descend into the next level (namespace, etc.)    
    if (auto innerDC = dyn_cast<DeclContext>(*I)) {
      processDeclContext(innerDC);
    }
  }
  popIndent();
}

void TypeRenameTransform::processStmt(Stmt *S)
{
  if (!S) {
    return;
  }
  
  pushIndent();
  // llvm::errs() << indent() << "Stmt: " << S->getStmtClassName() << ", at: "<< loc(S->getLocStart()) << "\n";
  
  if (auto CE = dyn_cast<CallExpr>(S)) {
    auto D = CE->getCalleeDecl();
    if (D) {
      // llvm::errs() << indent() << D->getDeclKindName() << "\n";
    }
  }
  if (auto E = dyn_cast<MemberExpr>(S)) {
    if (E->hasExplicitTemplateArgs()) {
      
      unsigned N = E->getNumTemplateArgs();
      auto TAL = E->getTemplateArgs();
      for (unsigned I = 0; I < N; ++I) {
        processTypeLoc(TAL[I].getTypeSourceInfo()->getTypeLoc());
      }
    }
  }
  else if (auto E = dyn_cast<CXXNewExpr>(S)) {
    processTypeLoc(E->getAllocatedTypeSourceInfo()->getTypeLoc());  
  }
  else if (auto E = dyn_cast<ExplicitCastExpr>(S)) {
    processTypeLoc(E->getTypeInfoAsWritten()->getTypeLoc());  
  }
  else if (auto E = dyn_cast<CXXTemporaryObjectExpr>(S)) {
    if (auto TSI = E->getTypeSourceInfo()) {
      processTypeLoc(TSI->getTypeLoc());  
    }
  }
  else if (auto E = dyn_cast<CXXUnresolvedConstructExpr>(S)) {
    if (auto TSI = E->getTypeSourceInfo()) {
      processTypeLoc(TSI->getTypeLoc());  
    }
  }
  else if (auto E = dyn_cast<VAArgExpr>(S)) {
    // TODO: This will be a problem if the arg is also a macro expansion...
    processTypeLoc(E->getWrittenTypeInfo()->getTypeLoc(), true);  
  }
  else if (auto E = dyn_cast<UnaryExprOrTypeTraitExpr>(S)) {
    // sizeof etc.
    if (E->isArgumentType()) {
      processTypeLoc(E->getArgumentTypeInfo()->getTypeLoc());  
    }
  }
  else if (auto E = dyn_cast<ObjCProtocolExpr>(S)) {
    // @protocol(X)
    std::string newName;
    if (nameMatches(E->getProtocol(), newName, true)) {
      renameLocation(E->getProtocolIdLoc(), newName);
    }
  }
  else if (auto E = dyn_cast<ObjCEncodeExpr>(S)) {
    // @encode(X)
    if (auto TSI = E->getEncodedTypeSourceInfo()) {
      processTypeLoc(TSI->getTypeLoc());  
    }    
  }
  else if (auto E = dyn_cast<ObjCMessageExpr>(S)) {
    // handle the case where [X alloc] and X in a type we want to rename
    if (E->getReceiverKind() == ObjCMessageExpr::Class) {
      if (auto TSI = E->getClassReceiverTypeInfo()) {
        processTypeLoc(TSI->getTypeLoc());  
      }          
    }
  }
  else {
    // TODO: Fill in other Stmt/Expr that has type info
    // TODO: Verify correctness and furnish test cases
  }
  
  for (auto I = S->child_begin(), E = S->child_end(); I != E; ++I) {
    processStmt(*I);
  }
  
  popIndent();
}

void TypeRenameTransform::processFunctionDecl(FunctionDecl *D)
{
  // if no type source info, it's a void f(void) function
  auto TSI = D->getTypeSourceInfo();
  if (TSI) {      
    processTypeLoc(TSI->getTypeLoc());
  }

  // handle ctor name initializers
  if (auto CD = dyn_cast<CXXConstructorDecl>(D)) {
    auto BL = CD->getLocation();
    std::string newName;
    if (BL.isValid() && CD->getParent()->getLocation() != BL && 
        nameMatches(CD->getParent(), newName, true)) {
      renameLocation(BL, newName);
    }
    
    for (auto II = CD->init_begin(), IE = CD->init_end(); II != IE; ++II) {
      if ((*II)->isBaseInitializer()) {
        processTypeLoc((*II)->getBaseClassLoc());        
      }
      
      auto X = (*II)->getInit();
      if (X) {
        processStmt(X);
      }
    }
  }
  
  // dtor
  if (auto DD = dyn_cast<CXXDestructorDecl>(D)) {
    // if parent matches
    auto BL = DD->getLocation();
    std::string newName;
    auto P = DD->getParent();
    if (BL.isValid() && P->getLocation() != BL &&
        nameMatches(P, newName, true)) {
    
      // can't use renameLocation since this is a tricy case        
    
      // need to use raw_identifier because Lexer::findLocationAfterToken
      // performs a raw lexing
      SourceLocation EL =
        Lexer::findLocationAfterToken(BL, tok::raw_identifier,
                                      sema->getSourceManager(),
                                      sema->getLangOpts(), false);


      // TODO: Find the right way to do this -- consider this a hack

      // llvm::errs() << indent() << "dtor at: " << loc(BL) << ", locStart: " << loc(DD->getLocStart())
      //   << ", nameAsString: " << P->getNameAsString() << ", len: " << P->getNameAsString().size()
      //   << ", DD nameAsString: " << DD->getNameAsString() << ", len: " << DD->getNameAsString().size()
      //   << "\n";
        
      if (EL.isValid()) {
        // EL is 1 char after the dtor name ~Foo, so -1 == pos of 'o'
        SourceLocation NE = EL.getLocWithOffset(-1);
        
        // we use the parent's name to see how much we should back off
        // if we use  D->getNameAsString(), we'd run into two problems:
        //   (1) the name would be ~Foo
        //   (2) the name for template class dtor would be ~Foo<T>
        SourceLocation NB =
          EL.getLocWithOffset(-(int)P->getNameAsString().size());

        if (NB.isMacroID()) {
          // TODO: emit error
          llvm::errs() << "Warning: Token is resulted from macro expansion"
            " and is therefore not renamed, at: " << loc(NB) << "\n";
        }
        else {
        // TODO: Determine if it's a wrtiable file
    
        // TODO: Determine if the location has already been touched or
        // needs skipping (such as in refactoring API user's code, then
        // the API headers need no changing since later the new API will be
        // in place)              
          replace(SourceRange(NB, NE), newName);
        }
      }
    }
  }
  
  // rename the params' types
  for (auto PI = D->param_begin(), PE = D->param_end(); PI != PE; ++PI) {
    processParmVarDecl(*PI);
  }
  
  // the name itself
  processQualifierLoc(D->getQualifierLoc());
  
  // handle body
  
  if (auto B = D->getBody()) {
    if (stmtInSameFileAsDecl(B, D)) {
      // llvm::errs() << indent() << "body? " << D->getBody() << "\n";
      processStmt(B);
    }
  }  
}

void TypeRenameTransform::processTypeLoc(TypeLoc TL, bool forceRewriteMacro)
{
  if (TL.isNull()) {
    return;
  }
  
  auto BL = TL.getBeginLoc();
  
  // ignore system headers
  if (shouldIgnore(BL)) {
    return;
  }
  
  // is a result from macro expansion? sorry...
  if (BL.isMacroID() && !forceRewriteMacro) {
    llvm::errs() << "Cannot rename type from macro expansion at: " << loc(BL) << "\n";
    return;
  }
  
  // TODO: Take care of spelling loc finesses
  // BL = sema->getSourceManager().getSpellingLoc(BL);

  pushIndent();
  auto QT = TL.getType();

  // llvm::errs() << indent()
  //   << "TypeLoc"
  //   << ", typeLocClass: " << typeLocClassName(TL.getTypeLocClass())
  //   << "\n" << indent() << "qualType as str: " << QT.getAsString()
  //   << "\n" << indent() << "beginLoc: " << loc(TL.getBeginLoc())
  //   << "\n";
    
  switch(TL.getTypeLocClass()) {    
    case TypeLoc::FunctionProto:
    {
      if (FunctionTypeLoc FTL = TL.getAs<FunctionTypeLoc>()) {
        for (unsigned I = 0, E = FTL.getNumArgs(); I != E; ++I) {
          processParmVarDecl(FTL.getArg(I));
        }
      }
      break;
    }    
    // an elaborated type loc captures the "prefix" of a type
    // for example, the elaborated type loc of "A::B::C" is A::B
    // we need to know if A::B and A are types we are renaming
    // (so that we can handle nested classes, in-class typedefs, etc.)
    case TypeLoc::Elaborated:
    {
      if (ElaboratedTypeLoc ETL = TL.getAs<ElaboratedTypeLoc>()) {
        processQualifierLoc(ETL.getQualifierLoc(), forceRewriteMacro);
      }
      break;
    }
    
    case TypeLoc::ObjCObject:
    {
      if (ObjCObjectTypeLoc OT = TL.getAs<ObjCObjectTypeLoc>()) {
        for (unsigned I = 0, E = OT.getNumProtocols(); I != E; ++I) {
          if (auto P = OT.getProtocol(I)) {
            std::string newName;
            if (nameMatches(P, newName, true)) {
              renameLocation(OT.getProtocolLoc(I), newName);
            }
          }
        }
      }
      break;
    }
    
    case TypeLoc::InjectedClassName:
    {
      if (InjectedClassNameTypeLoc TSTL = TL.getAs<InjectedClassNameTypeLoc>()) {
        auto CD = TSTL.getDecl();
        std::string newName;
        if (nameMatches(CD, newName, true)) {
          renameLocation(BL, newName);          
        }      
      }
      break;
    }
    
    case TypeLoc::TemplateSpecialization:
    {
      if (TemplateSpecializationTypeLoc TSTL = TL.getAs<TemplateSpecializationTypeLoc>()) {
        
        // See if it's the template name that needs renaming
        auto T = TL.getTypePtr();
        
        if (auto TT = dyn_cast<TemplateSpecializationType>(T)) {
          auto TN = TT->getTemplateName();
          auto TD = TN.getAsTemplateDecl();
          auto TTD = TD->getTemplatedDecl();

          std::string newName;
          if (nameMatches(TTD, newName, true)) {
            renameLocation(TSTL.getTemplateNameLoc(), newName);
          }
        }

        // iterate through the args
        for (unsigned I = 0, E = TSTL.getNumArgs(); I != E; ++I) {
          
          // need to see if the template argument is also a type
          // (we skip things like Foo<1> )
          auto AL = TSTL.getArgLoc(I);
          auto A = AL.getArgument();
          if (A.getKind() != TemplateArgument::Type) {
            continue;
          }
          
          if (auto TSI = AL.getTypeSourceInfo()) {
            processTypeLoc(TSI->getTypeLoc(), forceRewriteMacro);
          }
        }
      }
      break;
    }
    
    // typedef is tricky
    case TypeLoc::Typedef:
    {
      auto T = TL.getTypePtr();
      if (auto TDT = dyn_cast<TypedefType>(T)) {
        auto TDD = TDT->getDecl();
        std::string newName;
        if (nameMatches(TDD, newName, true)) {
          renameLocation(BL, newName);
        }
      }
      
      break;
    }
    
    // leaf types
    // TODO: verify correctness, need test cases for each    
    // TODO: Check if Builtin works
    case TypeLoc::Builtin:
    case TypeLoc::Enum:    
    case TypeLoc::Record:
    case TypeLoc::ObjCInterface:
    case TypeLoc::TemplateTypeParm:
    {
      // skip if it's an anonymous type
      // read Clang`s definition (in RecordDecl) -- not exactly what you think
      // so we use the length of name

      std::string newName;      
      if (auto TT = dyn_cast<TagType>(TL.getTypePtr())) {
        auto TD = TT->getDecl();
        if (nameMatches(TD, newName, true)) {
          renameLocation(BL, newName);          
        }
      }
      else {
        if (stringMatches(QT.getAsString(), newName)) {
          renameLocation(BL, newName);
        }
      }
      break;
    }
      
      
    default:
      break;
  }
  
  processTypeLoc(TL.getNextTypeLoc(), forceRewriteMacro);
  popIndent();
}

// fix the "B" part of names like A::B::C if B is our target
void TypeRenameTransform::processQualifierLoc(NestedNameSpecifierLoc NNSL,
                                              bool forceRewriteMacro)
{
  while (NNSL) {
    auto NNS = NNSL.getNestedNameSpecifier();
    auto NNSK = NNS->getKind();
    if (NNSK == NestedNameSpecifier::TypeSpec || NNSK == NestedNameSpecifier::TypeSpecWithTemplate) {
      processTypeLoc(NNSL.getTypeLoc(), forceRewriteMacro);
    }
    
    NNSL = NNSL.getPrefix();
  }  
}

void TypeRenameTransform::processParmVarDecl(ParmVarDecl *P)
{
  auto PTSI = P->getTypeSourceInfo();
  if (PTSI) {
    processTypeLoc(PTSI->getTypeLoc());
  }
  
  // then the default vars
  if (P->hasInit()) {
    processStmt(P->getInit());
  }  
}
