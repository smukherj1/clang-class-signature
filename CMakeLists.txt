set(LLVM_LINK_COMPONENTS support)

add_clang_executable(class-version
    ClassVersion.cpp
)

target_link_libraries(class-version
    clangTooling
    clangBasic
    clangAST
    clangASTMatchers
    clang
)
