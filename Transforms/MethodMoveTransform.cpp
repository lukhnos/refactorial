//
// MethodMoveTransform.cpp: Batch method move
//

#include "Transforms.h"

#include <set>
#include <clang/Basic/SourceManager.h>
#include <clang/Sema/Sema.h>
#include <llvm/Support/raw_ostream.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Rewrite/Core/Rewriter.h>

using namespace clang;

class MethodMoveTransform : public Transform {
public:  
	virtual void HandleTranslationUnit(ASTContext &C);
	
  
protected:
	std::string movingClassName;
  
	void processDeclContext(DeclContext *DC);
	void processCXXRecordDecl(CXXRecordDecl *CRD);
	void collectNamespaceInfo(DeclContext *DC, SourceLocation& EL,
	                          std::string& outHeader,
	                          std::string& outFooter);
	std::string rewriteMethodInHeader(CXXMethodDecl *M);
  
protected:
	// TODO: move these to a utility
	std::string captureSourceText(SourceLocation B, SourceLocation E,
	                              bool endBeyondToken = false);
	                              
  std::string removeHeadIndent(const std::string& src);
  static std::vector<std::string> split(const std::string& text, char c);
};

REGISTER_TRANSFORM(MethodMoveTransform);

  
void MethodMoveTransform::HandleTranslationUnit(ASTContext &C)
{
	auto movingSpec = TransformRegistry::get().config["MethodMove"].begin();
	movingClassName = movingSpec->first.as<std::string>();
	clang::SourceManager &SM = C.getSourceManager();
	clang::FileManager &FM = SM.getFileManager();
	if(FM.getFile(movingSpec->second.as<std::string>()) == SM.getFileEntryForID(SM.getMainFileID()))
	{
		llvm::errs() << "MovingClassName: " << movingClassName << "\n";
		llvm::errs() << SM.getFileEntryForID(SM.getMainFileID())->getName() << "\n";
		auto TUD = C.getTranslationUnitDecl();
		processDeclContext(TUD);
	}
}
  
void MethodMoveTransform::processDeclContext(DeclContext *DC)
{  
	for(auto I = DC->decls_begin(), E = DC->decls_end(); I != E; ++I) {    
		if(auto CRD = dyn_cast<CXXRecordDecl>(*I)) {
			processCXXRecordDecl(CRD);
		}
		else if (auto innerDC = dyn_cast<DeclContext>(*I)) {
			// descend into the next level (namespace, etc.)
			processDeclContext(innerDC);
		}
	}
}

void MethodMoveTransform::processCXXRecordDecl(CXXRecordDecl *CRD)
{
	// // TODO: handle nested class
	// processDeclContext(CRD);
  
	if (movingClassName != CRD->getQualifiedNameAsString()) {
		return;
	}

	llvm::errs() << "Rewriting: " << CRD->getQualifiedNameAsString() << "\n";
  
	// the right brace location of this CXXRecordDecl
	// nothing beyond that location is taken into consideration
	auto CRDRBL = CRD->getRBraceLoc();
  
	std::string sourceHeader;
	std::string sourceFooter;
	std::string aggregateSource;
	collectNamespaceInfo(CRD->getParent(), CRDRBL, sourceHeader, sourceFooter);

	aggregateSource += "\n";
  
	aggregateSource += "\n\n";
	aggregateSource += "// ";
	aggregateSource += std::string(70, '-');
	aggregateSource += "\n// refactorial MethodMoveTransform output\n";
	aggregateSource += "// ";
	aggregateSource += std::string(70, '-');
	aggregateSource += "\n\n";
  
	aggregateSource += sourceHeader;
	aggregateSource += "\n\n";

	// methods, this include all ctors and dtors
	for (auto MI = CRD->method_begin(), ME = CRD->method_end();
	     MI != ME; ++MI) {

		if (0) {
			continue;
		}
    
		// we're only interested in inline, defined-in-class-decl methods
		if (!MI->hasBody()) {
			continue;
		}
    
		// body is a Stmt (compound statement)
		auto S = MI->getBody();
    
		// is the inline def outside of the record body? if not, skip
		// this also requires the check whether both are from the same file
		auto SLS = S->getLocStart();
		if (!sema->getSourceManager().isFromSameFile(CRDRBL, SLS)) {
			continue;
		}
      
		if (CRDRBL < SLS) {
			continue;
		}
    
		std::string B = rewriteMethodInHeader(*MI);
		aggregateSource += B;
		aggregateSource += "\n";
	}
  
	aggregateSource += sourceFooter;


	// write the source
	auto MFI = sema->getSourceManager().getMainFileID();
	auto LEOF = sema->getSourceManager().getLocForEndOfFile(MFI);
	
	insert(LEOF, aggregateSource);
}

void MethodMoveTransform::collectNamespaceInfo(DeclContext *DC,
                                               SourceLocation& EL,
                                               std::string& outHeader,
                                               std::string& outFooter)                                               
{
	if (!DC) {
		return;
	}

	collectNamespaceInfo(DC->getParent(), EL, outHeader, outFooter);
  
	if (auto NSD = dyn_cast<NamespaceDecl>(DC)) {
		outHeader += "namespace ";
		outHeader += NSD->getNameAsString();
		outHeader += " {\n";
    
		std::string footer = "}; // namespace ";
		footer += NSD->getNameAsString();    
		footer += "\n";
		outFooter.insert(0, footer);
	}
  
	// TODO: Insert indent
  
	// TODO: Discuss on the mailing list, these iterators only gives the *last*
	// instance of using, e.g.:
	//
	//   namespace A {
	//     using namespace B;
	//     class Foo { /* ... */ };
	//     using namespace B;  // only this is given
	//   };
	//
	// but apparently class Foo is already using namespace B; this will cause
	// some problem for our namespace replication
  
	for (auto UI = DC->using_directives_begin(),
		     UE = DC->using_directives_end(); UI != UE; ++UI) {
  
		auto UDLS = (*UI)->getLocStart();
		if (!sema->getSourceManager().isFromSameFile(EL, UDLS)) {
			continue;
		}
    
		if (EL < UDLS) {
			continue;
		}
         
		auto ND = (*UI)->getNominatedNamespaceAsWritten();
		auto N = ND->getNameAsString();
    
		outHeader += "using namespace ";
		outHeader += N;
		outHeader += ";\n";
	}
}

std::string MethodMoveTransform::rewriteMethodInHeader(CXXMethodDecl *M)
{
	std::string src;
	llvm::raw_string_ostream sst(src);
  
	// return type (but no type if ctor or dtor)
	if (!isa<CXXConstructorDecl>(M) && !isa<CXXDestructorDecl>(M)) {
		sst << M->getResultType().getAsString();
		sst << " ";
	}

	// method name
	sst << M->getParent()->getDeclName().getAsString();
	sst << "::";
	sst << M->getDeclName().getAsString();
  
	// parameter list
	auto PI = M->param_begin();
	auto PE = M->param_end();
	auto LPE = PI;

	sst << "(";
  
	while (PI != PE) {
		// get param type
		auto PIB = (*PI)->getSourceRange().getBegin();
		auto PTSI = (*PI)->getTypeSourceInfo();
		auto PTL = PTSI->getTypeLoc();
		auto PTLE = sema->getPreprocessor().getLocForEndOfToken(PTL.getEndLoc());
		sst << captureSourceText(PIB, PTLE, true);

		auto PN = (*PI)->getName();
		if (PN.size()) {
			// determine if we need to insert a space between type and param
			// this deals with the variants of T* x, T* x, etc.
			const char *cdataEnd = sema->getSourceManager().getCharacterData(PTLE);
      
			if (*cdataEnd == ' ' || *cdataEnd == '\t') {      
				sst << " ";
			}
      
			sst << PN;
		}

		// keep track of the last param
		LPE = PI;
		++PI;
		if (PI != PE) {
			sst << ", ";
		}
	}

	sst << ")";

	// insert the const qualifier
	if (M->getTypeQualifiers() & Qualifiers::Const) {
		sst << " const";
	}

	// obtain the method's type info
	auto MTSI = M->getTypeSourceInfo();
	auto MTL = MTSI->getTypeLoc();
	auto MTLE = sema->getPreprocessor().getLocForEndOfToken(MTL.getEndLoc());

	// obtain the end of the body
	SourceLocation MBE = M->getBody()->getLocEnd();

	// we capture everything between the location past the method's type
	// and the right brace of the body, inclusive; this has a desired side
	// effect: the ctor's initializers (if exist) will also be included
  auto capturedSrc = captureSourceText(MTLE, MBE);
  auto reformattedSrc = removeHeadIndent(capturedSrc);
	
  sst << reformattedSrc;
	sst << "\n";

	// replace the body with ;
	SourceRange replaceRange(MTLE, MBE);
	replace(replaceRange, ";");

	// return the captured source  
	// TODO: re-indent the source  
	return sst.str();
}

std::string MethodMoveTransform::captureSourceText(SourceLocation B,
                                                   SourceLocation E,
                                                   bool endBeyondToken)
{
	const char *cdataBegin = sema->getSourceManager().getCharacterData(B);
	const char *cdataEnd = sema->getSourceManager().getCharacterData(E);
	return std::string(cdataBegin,
	                   cdataEnd - cdataBegin + (endBeyondToken ? 0 : 1));
}

std::string MethodMoveTransform::removeHeadIndent(const std::string& src)
{
  std::string newSrc;
  std::string result;

  bool hasInitializer = false;
  // always see if it's an initializer and see if it has some leading space

  for (auto SI = src.begin(), SE = src.end(); SI != SE; ++SI) {
    auto c = *SI;
    if (c != ' ' && c != '\t') {
      newSrc.insert(newSrc.end(), SI, SE);
      break;
    }
  }
  
  if (!newSrc.size()) {
    return result;
  }
  
  if (newSrc[0] == ':') {
    hasInitializer = true;
  }
  

  size_t tabSize = 2;
  std::vector<int> indentPos;
  std::vector<std::string> lines = split(newSrc, '\n');
  
  size_t minHeadIndent = tabSize * 100;
  for (auto I = lines.begin(), E = lines.end(); I != E; ++I) {
    std::string &S = *I;
    
    llvm::errs() << "src: " << *I << "\n";
    
    size_t ws = 0;
    for (auto SI = S.begin(), SE = S.end(); SI != SE; ++SI) {
      auto c = *SI;
      if (c != ' ' && c != '\t') {
        break;
      }
      
      if (c == ' ') {
        ws++;
      }
      else {
        size_t rm = ws % tabSize;
        ws += (rm ? rm : tabSize);
      }
    }
    
    if (ws && ws < minHeadIndent) {
      minHeadIndent = ws;
    }
  }


  if (minHeadIndent < tabSize) {
    minHeadIndent = tabSize;
  }

  llvm::errs() << "min head indent: " << minHeadIndent << "\n";

  for (auto I = lines.begin(), E = lines.end(); I != E; ++I) {
    std::string &S = *I;
    
    size_t ws = 0;
    for (auto SI = S.begin(), SE = S.end(); SI != SE; ++SI) {
      auto c = *SI;
      
      if ((ws && ws >= minHeadIndent) || (c != ' ' && c != '\t')) {
        result.insert(result.end(), SI, SE);
        break;
      }
      
      if (c == ' ') {
        ws++;
      }
      else {
        size_t rm = ws % tabSize;
        ws += (rm ? rm : tabSize);
      }
    }
    
    auto J = I;
    if (++J != lines.end()) {
      result += "\n";
    }
  }
  
  if (hasInitializer) {
    result.insert(0, std::string(tabSize, ' '));
  }

  result.insert(0, "\n");
  
  return result;
}

std::vector<std::string> MethodMoveTransform::split(const std::string& text,
                                                    char c)
{
    std::vector<std::string> result;
    size_t index = 0, last = 0, length = text.length();
    while (index < length) {
        while (index < length) {
            if (text[index] == c) {
                result.push_back(text.substr(last, index - last));
                last = index + 1;
                break;
            }
            index++;            
        }

        index++;
    }

    if (last <= index) {
        result.push_back(text.substr(last, index - last));           
    }

    return result;
}                                

