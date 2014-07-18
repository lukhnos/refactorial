#include "Transforms.h"

#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>

#include <stdexcept>

using namespace clang;
using namespace clang::tooling;
using namespace std;

void Transform::InitializeSema(Sema &s)
{
	sema = &s;
}

void Transform::insert(SourceLocation loc, string text)
{
	TransformRegistry::get().replacements->push_back(Replacement(sema->getSourceManager(), CharSourceRange(SourceRange(loc, loc), false), text));
}

void Transform::replace(SourceRange range, string text)
{
	TransformRegistry::get().replacements->push_back(Replacement(sema->getSourceManager(), CharSourceRange(range, true), text));
}

TransformRegistry &TransformRegistry::get()
{
	static TransformRegistry instance;
	return instance;
}

void TransformRegistry::add(const string &name, transform_creator creator)
{
	m_transforms.insert(pair<string, transform_creator>(name, creator));
}

const transform_creator TransformRegistry::operator[](const string &name) const
{
	auto iter = m_transforms.find(name);
	if(iter==m_transforms.end())
		throw out_of_range(name);
	return iter->second;
}

class TransformAction : public ASTFrontendAction {
private:
	transform_creator tcreator;
public:
	TransformAction(transform_creator creator) {tcreator = creator;}
protected:
	ASTConsumer *CreateASTConsumer(CompilerInstance &CI, llvm::StringRef) {
		return tcreator();
	}
};

TransformFactory::TransformFactory(transform_creator creator) {
	tcreator = creator;
}
FrontendAction *TransformFactory::create() {
	return new TransformAction(tcreator);
}
