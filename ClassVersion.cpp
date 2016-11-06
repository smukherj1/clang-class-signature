#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include <fstream>

using namespace clang;
using namespace clang::tooling;

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory MyToolCategory("my-tool options");

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static llvm::cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static llvm::cl::extrahelp MoreHelp("\nMore help text...");

static llvm::cl::opt<std::string> OutputFilename("o", llvm::cl::desc("Specify output filename"),
                                                 llvm::cl::value_desc("filename"),
                                                 llvm::cl::init("-"));

static llvm::cl::list<std::string> matchList("m", llvm::cl::ZeroOrMore);

template <class TargetType, class AppendType>
void appendVal(TargetType &t, const AppendType &a, int num)
{
    for (int i = 0; i < num; ++i)
    {
        t += a;
    }
}

struct FieldDatabase
{
    std::string type;
    std::string variable;

    template <class OstreamType> OstreamType &dump(OstreamType &out, int indent = 0) const
    {
        std::string indent_str;
        std::string member_indent_str;
        appendVal(indent_str, " ", indent);
        appendVal(member_indent_str, " ", indent + 4);
        out << indent_str << "{\n";
        out << member_indent_str << "\"type\" : \"" << type << "\",\n";
        out << member_indent_str << "\"variable\": \"" << variable << "\"";
        out << "\n" << indent_str << "}";
        return out;
    }
};

class ClassDatabase
{
  public:
    ClassDatabase(std::string &&name) : m_name(name) {}
    FieldDatabase &addField()
    {
        m_fields.emplace_back(FieldDatabase());
        return m_fields.back();
    }

    const std::string &name_ref() const { return m_name; }

    std::string name() const { return m_name; }

    template <class OstreamType> OstreamType &dump(OstreamType &out, int indent = 0) const
    {
        std::string indent_str;
        std::string member_indent_str;
        appendVal(indent_str, " ", indent);
        appendVal(member_indent_str, " ", indent + 4);
        out << indent_str << "{\n";
        out << member_indent_str << "\"name\": \"" << m_name << "\",\n";
        if (m_fields.empty())
        {
            out << member_indent_str << "\"fields\": []";
        }
        else
        {
            out << member_indent_str << "\"fields\":\n";
            out << member_indent_str << "[\n";
            int iter = 0;
            for (const FieldDatabase &fdb : m_fields)
            {
                if (iter > 0)
                {
                    out << ",\n";
                }
                iter++;
                fdb.dump(out, indent + 8);
            }
            out << "\n" << member_indent_str << "]";
        }
        out << "\n" << indent_str << "}";
        return out;
    }

  private:
    std::vector<FieldDatabase> m_fields;
    std::string m_name;
};

class ToolDatabase
{
  public:
    ClassDatabase &addClass(std::string &&name)
    {
        m_classes.emplace_back(ClassDatabase(std::forward<std::string>(name)));
        return m_classes.back();
    }

    template <class OstreamType> OstreamType &dump(OstreamType &out, int indent = 0) const
    {
        std::string indent_str;
        appendVal(indent_str, " ", indent);
        out << "\n" << indent_str << "[\n";
        int iter = 0;
        for (const auto &cdb : m_classes)
        {
            if (iter > 0)
            {
                out << ",\n";
            }
            iter++;
            cdb.dump(out, indent + 4);
        }
        out << "\n" << indent_str << "]";
        return out;
    }

  private:
    std::vector<ClassDatabase> m_classes;
};

ToolDatabase global_tdb;

class FindNamedClassVisitor : public RecursiveASTVisitor<FindNamedClassVisitor>
{
  private:
    bool shouldVisit(CXXRecordDecl *Declaration)
    {
        if (matchList.empty())
        {
            // No matching list specified. Visit everything
            return true;
        }

        std::string class_name = Declaration->getQualifiedNameAsString();

        for (const auto &m : matchList)
        {
            if (class_name.find(m) != std::string::npos)
            {
                return true;
            }
        }

        return false;
    }

  public:
    explicit FindNamedClassVisitor(ASTContext *Context) : Context(Context) {}

    bool VisitCXXRecordDecl(CXXRecordDecl *Declaration)
    {
        if (!shouldVisit(Declaration))
        {
            return true;
        }
        ClassDatabase &cdb = global_tdb.addClass(Declaration->getQualifiedNameAsString());
        for (const FieldDecl *fdcl : Declaration->fields())
        {
            FieldDatabase &fdb = cdb.addField();
            fdb.type = fdcl->getType().getAsString();
            fdb.variable = fdcl->getQualifiedNameAsString();
        }
        return true;
    }

  private:
    ASTContext *Context;
};

class FindNamedClassConsumer : public clang::ASTConsumer
{
  public:
    explicit FindNamedClassConsumer(ASTContext *Context) : Visitor(Context) {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context)
    {
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    }

  private:
    FindNamedClassVisitor Visitor;
};

class FindNamedClassAction : public clang::ASTFrontendAction
{
  public:
    virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &Compiler,
                                                                  llvm::StringRef InFile)
    {
        return std::unique_ptr<clang::ASTConsumer>(
            new FindNamedClassConsumer(&Compiler.getASTContext()));
    }
};

int dump_tool_database()
{
    if (OutputFilename == "-")
    {
        global_tdb.dump(llvm::outs());
        llvm::outs() << "\n";
        return 0;
    }

    std::ofstream out(OutputFilename.c_str());
    if (out.fail() || out.bad())
    {
        llvm::errs() << "Failed to open output file " << OutputFilename << " for writing.";
        return 1;
    }

    global_tdb.dump(out);

    return 0;
}

int main(int argc, const char **argv)
{
    CommonOptionsParser OptionsParser(argc, argv, MyToolCategory);
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    int result = Tool.run(newFrontendActionFactory<FindNamedClassAction>().get());
    if (result == 0)
    {
        result = dump_tool_database();
    }
    return result;
}