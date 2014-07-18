#include "Transforms.h"

#include <map>

using namespace clang;
using namespace std;


class ExtractParameterTransform : public Transform {
public:  
	virtual void HandleTranslationUnit(ASTContext &C);
	
  
protected:
	
	string extractMethodName;
	string extractVariableName;
	string extractDefaultValue;

	map<const FunctionDecl*, list<const VarDecl*> > rewrites;
  
	void collectDeclContext(const DeclContext *DC);
	void collectFunction(const FunctionDecl *FN);

	void process();
	void removeDecl(const Stmt *stmt, const VarDecl *decl);
};

REGISTER_TRANSFORM(ExtractParameterTransform);
  
void ExtractParameterTransform::HandleTranslationUnit(ASTContext &C)
{
	auto extractSpec = TransformRegistry::get().config["ExtractParameter"].begin();
	extractMethodName = (*extractSpec)["method"].as<string>();
	extractVariableName = (*extractSpec)["variable"].as<string>();
	extractDefaultValue = "";
	if((*extractSpec)["default"])
		extractDefaultValue = (*extractSpec)["default"].as<string>();

	collectDeclContext(C.getTranslationUnitDecl());
	process();
}
  
void ExtractParameterTransform::collectDeclContext(const DeclContext *DC)
{  
	for(auto I = DC->decls_begin(), E = DC->decls_end(); I != E; ++I) {
		if (auto innerFN = dyn_cast<FunctionDecl>(*I))
		{
			collectFunction(innerFN);
		}
		if (auto innerDC = dyn_cast<DeclContext>(*I)) {
			// descend into the next level (namespace, etc.)
			collectDeclContext(innerDC);
		}

	}
}

void ExtractParameterTransform::collectFunction(const FunctionDecl *FN)
{
	if(FN->getQualifiedNameAsString() == extractMethodName)
	{
		for(auto I = FN->decls_begin(), E = FN->decls_end(); I != E; ++I)
		{
			if(auto valueDecl = dyn_cast<VarDecl>(*I))
			{
				if(valueDecl->getNameAsString() == extractVariableName)
				{
					rewrites[FN].push_back(valueDecl);
				}
			}
		}
	}
}

void ExtractParameterTransform::process()
{
	for(auto RI = rewrites.begin(), RE = rewrites.end(); RI != RE; ++RI)
	{
		const FunctionDecl *FN = RI->first;
		list<const VarDecl*> values = RI->second;
		SourceLocation insertionLoc;

		if(FN->getNumParams()>0)
		{
			const ParmVarDecl *lastParam = FN->getParamDecl(FN->getNumParams()-1);
			insertionLoc = lastParam->getLocEnd();
		}
		else
		{
			TypeLoc TL = FN->getTypeSourceInfo()->getTypeLoc();
			if (FunctionTypeLoc funTypeLoc = TL.getAs<FunctionTypeLoc>()) {
				SourceLocation lParenLoc = funTypeLoc.getLocalRangeBegin();
				insertionLoc = getLocForEndOfToken(lParenLoc);
			}
		}
		vector<YAML::Node> transformData = TransformRegistry::get().config["ExtractParameter"].as<vector<YAML::Node> >();
		for(auto CI = transformData.begin(), CE = transformData.end(); CI != CE; ++CI)
		{
			YAML::Node node = *CI;
			if(node["method"].as<string>() == FN->getQualifiedNameAsString())
			{
				for(auto VI = values.begin(), VE = values.end(); VI != VE; ++VI)
				{
					if(node["variable"].as<string>() == (*VI)->getNameAsString())
					{
						string str = (*VI)->getType().getAsString() + " " + (*VI)->getNameAsString()
							+ " = " + node["default"].as<string>();
						insert(insertionLoc, str);
						if(FN->hasBody())
							removeDecl(FN->getBody(), *VI);
						break;
					}
				}
			}
		}
	}
}

void ExtractParameterTransform::removeDecl(const Stmt *stmt, const VarDecl *decl)
{
	assert(isa<CompoundStmt>(stmt));
	const CompoundStmt *cstmt = dyn_cast<CompoundStmt>(stmt);
	for(auto substmt = cstmt->body_begin(); substmt != cstmt->body_end(); ++substmt)
	{
		if(const DeclStmt *dstmt = dyn_cast<DeclStmt>(*substmt))
		{
			const DeclGroupRef DGR = dstmt->getDeclGroup();
			for(auto test_decl = DGR.begin(); test_decl != DGR.end(); ++test_decl)
			{
				if(*test_decl == decl)
				{
					SourceRange rangeToRemove = decl->getSourceRange();
					
					if(DGR.isSingleDecl())
					{
						rangeToRemove.setEnd(findLocAfterSemi(rangeToRemove.getEnd()));
					}
					else
					{
						if(test_decl==DGR.begin())
						{
							//first decl includes type--we must remove it, and add a comma at the end
							TypeLoc TL = decl->getTypeSourceInfo()->getTypeLoc();
							rangeToRemove.setBegin(getLocForEndOfToken(TL.getLocEnd()));
							rangeToRemove.setEnd(Lexer::findLocationAfterToken(rangeToRemove.getEnd(), clang::tok::comma, sema->getSourceManager(), sema->getLangOpts(), false).getLocWithOffset(-1));
						}
						else
						{
							//any other decl--we must add a comma at the beginning
							rangeToRemove.setBegin(getLocForEndOfToken((decl-1)->getSourceRange().getEnd()));
						}
					}
					
					replace(rangeToRemove, " ");
				}
			}
		}
	}
}
