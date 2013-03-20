CMAKE_MINIMUM_REQUIRED (VERSION 2.8)

PROJECT (Refactorial)

SET(CMAKE_DISABLE_SOURCE_CHANGES ON)
SET(CMAKE_DISABLE_IN_SOURCE_BUILD ON)
SET(CMAKE_BUILD_TYPE None)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_LINKER_FLAGS} -O0 -g -fno-rtti -fcolor-diagnostics -Wno-c++11-extensions -DBOOST_NO_TYPEID -DBOOST_NO_RTTI")
SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_CXX_LINKER_FLAGS} -g -fno-rtti")
IF(APPLE)
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I/opt/local/include")
  # SET(CMAKE_C_COMPILER /usr/local/bin/clang)
  # SET(CMAKE_CXX_COMPILER /usr/local/bin/clang++)
  # SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ --std=c++0x -I/opt/local/include")
  # SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -stdlib=libc++")
  SET(CLANG_LIBRARIES 
    clangAnalysis
    clangARCMigrate
    clangAST
    clangASTMatchers
    clangBasic
    clangCodeGen
    clangDriver
    clangEdit
    clangFrontend
    clangFrontendTool
    clangLex
    clangParse
    clangRewriteCore
    clangRewriteFrontend
    clangSema
    clangSerialization
    clangStaticAnalyzerCheckers
    clangStaticAnalyzerCore
    clangStaticAnalyzerFrontend
    clangTooling)
ELSE(APPLE)
  SET(CMAKE_C_COMPILER clang)
  SET(CMAKE_CXX_COMPILER clang++)
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  SET(CLANG_LIBRARIES clang)
ENDIF(APPLE)

FIND_PACKAGE(LLVM REQUIRED)
FIND_LIBRARY(PCRE_LIBRARY pcre)
FIND_LIBRARY(PCRECPP_LIBRARY pcrecpp)

ADD_DEFINITIONS(${LLVM_DEFINITIONS})
INCLUDE_DIRECTORIES(${LLVM_INCLUDE_DIRS} .)
LINK_DIRECTORIES(${LLVM_LIBRARY_DIRS})
LLVM_MAP_COMPONENTS_TO_LIBRARIES(REQ_LLVM_LIBRARIES arminfo cellspuinfo cppbackendinfo debuginfo hexagoninfo mblazeinfo mipsinfo msp430info nvptxinfo powerpcinfo sparcinfo x86info xcoreinfo armdesc cellspudesc hexagondesc mblazedesc mipsdesc msp430desc nvptxdesc powerpcdesc sparcdesc x86desc xcoredesc armasmparser asmparser mblazeasmparser mcparser mipsasmparser x86asmparser)


SET(Transforms_sources
  AccessorsTransform.cpp
  ExtractParameterTransform.cpp
  FunctionRenameTransform.cpp
  IdentityTransform.cpp
  MethodMoveTransform.cpp
  RecordFieldRenameTransform.cpp
  Transforms.cpp
  TypeRenameTransform.cpp
)

FOREACH(arg ${Transforms_sources})
  LIST(APPEND sources "Transforms/${arg}")
ENDFOREACH(arg ${Transforms_sources})

SET(sources ${sources} main.cpp Refactoring.cpp)

ADD_EXECUTABLE (refactorial ${sources} )
TARGET_LINK_LIBRARIES (refactorial ${REQ_LLVM_LIBRARIES} ${CLANG_LIBRARIES} ${PCRE_LIBRARY} ${PCRECPP_LIBRARY} yaml-cpp)
