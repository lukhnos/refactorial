#ifndef TRANSFORM_H
#define TRANSFORM_H

#include <string>
#include <vector>
#include <stdint.h>

#include <clang/AST/ASTContext.h>
#include <clang/Lex/Lexer.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Sema/Sema.h>
#include <clang/Sema/SemaConsumer.h>
#include <clang/Tooling/Tooling.h>
#include <clang/Frontend/CompilerInstance.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>

#include "Refactoring.h"

#include <yaml-cpp/yaml.h>
#include "yaml-util.h"

class Transform : public clang::SemaConsumer
{
protected:
	clang::Sema *sema;
	virtual void InitializeSema(clang::Sema &s);
	friend class TransformFactory;
	void insert(clang::SourceLocation loc, std::string text);
	void replace(clang::SourceRange range, std::string text);
	clang::SourceLocation findLocAfterToken(clang::SourceLocation curLoc, clang::tok::TokenKind tok) {
		return clang::Lexer::findLocationAfterToken(curLoc, tok, sema->getSourceManager(), sema->getLangOpts(), true);
	}
	clang::SourceLocation getLocForEndOfToken(clang::SourceLocation curLoc) {
		return clang::Lexer::getLocForEndOfToken(curLoc, 0, sema->getSourceManager(), sema->getLangOpts());
	}
	clang::SourceLocation findLocAfterSemi(clang::SourceLocation curLoc) {return findLocAfterToken(curLoc, clang::tok::semi);}
};

template <typename T> Transform* transform_factory()
{
	return new T;
}

typedef Transform* (*transform_creator)(void);

class TransformRegistry
{
 private:
	std::map<std::string,transform_creator> m_transforms;
 public:
	YAML::Node config;
	std::map<std::string, std::string> touchedFiles;
	Replacements *replacements;
	
	static TransformRegistry& get();
	void add(const std::string &, transform_creator);
	const transform_creator operator[](const std::string &name) const;
};

class TransformRegistration
{
public:
	TransformRegistration(const std::string& name, transform_creator creator) {
		TransformRegistry::get().add(name, creator);
	}
};

#define REGISTER_TRANSFORM(transform)	  \
	TransformRegistration _transform_registration_ \
	## transform(#transform, &transform_factory<transform>)

class TransformFactory : public clang::tooling::FrontendActionFactory {
private:
	transform_creator tcreator;
public:
	TransformFactory(transform_creator creator);
	clang::FrontendAction *create();
};

#endif
