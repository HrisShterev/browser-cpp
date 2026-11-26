#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>
#include <string>
#include <vector>

using namespace clang;
using namespace clang::tooling;

static llvm::cl::OptionCategory TracerCategory("cpp-tracer options");

struct TrackedVar {
    std::string name;
    bool isPointer;
    bool isArray;
    uint64_t arraySize;
};

class TraceASTVisitor : public RecursiveASTVisitor<TraceASTVisitor> {
public:
    explicit TraceASTVisitor(ASTContext* Context, Rewriter& R)
        : Context(Context), TheRewriter(R) {}

    // 1. При вход във функция вмъкваме __trace_enter_func
    bool VisitFunctionDecl(FunctionDecl* Func) {
        if (!Func->hasBody() || !Context->getSourceManager().isInMainFile(Func->getLocation()))
            return true;

        activeVars.clear();
        Stmt* Body = Func->getBody();
        SourceLocation StartLoc = Body->getBeginLoc().getLocWithOffset(1);
        unsigned Line = Context->getSourceManager().getSpellingLineNumber(Func->getLocation());

        std::string EnterCall = "\n    __trace_enter_func(\"" + Func->getNameAsString() + "\", " + std::to_string(Line) + ");";
        TheRewriter.InsertTextAfter(StartLoc, EnterCall);
        return true;
    }

    // 2. Следим декларациите на променливи и ги добавяме към списъка за проследяване
    bool VisitDeclStmt(DeclStmt* DS) {
        if (!Context->getSourceManager().isInMainFile(DS->getBeginLoc()))
            return true;

        for (Decl* D : DS->decls()) {
            if (auto* VD = dyn_cast<VarDecl>(D)) {
                QualType QT = VD->getType();
                TrackedVar TV;
                TV.name = VD->getNameAsString();
                TV.isPointer = QT->isPointerType();
                TV.isArray = QT->isConstantArrayType();
                TV.arraySize = 0;

                if (TV.isArray) {
                    if (const auto* CAT = Context->getAsConstantArrayType(QT)) {
                        TV.arraySize = CAT->getSize().getZExtValue();
                    }
                }
                activeVars.push_back(TV);
            }
        }

        insertTraceAfterStmt(DS);
        return true;
    }

    // 3. След всяко присвояване или инкрементиране (напр. p++, x = 5) вмъкваме стъпка
    bool VisitBinaryOperator(BinaryOperator* BO) {
        if (!Context->getSourceManager().isInMainFile(BO->getBeginLoc()))
            return true;
        if (BO->isAssignmentOp()) {
            insertTraceAfterStmt(BO);
        }
        return true;
    }

    bool VisitUnaryOperator(UnaryOperator* UO) {
        if (!Context->getSourceManager().isInMainFile(UO->getBeginLoc()))
            return true;
        if (UO->isIncrementDecrementOp()) {
            insertTraceAfterStmt(UO);
        }
        return true;
    }

    bool VisitReturnStmt(ReturnStmt* RS) {
        if (!Context->getSourceManager().isInMainFile(RS->getBeginLoc()))
            return true;
        TheRewriter.InsertTextBefore(RS->getBeginLoc(), 
            "std::cout << TracerRuntime::to_json() << std::endl;\n    __trace_exit_func();\n    ");
        return true;
    }

private:
    ASTContext* Context;
    Rewriter& TheRewriter;
    std::vector<TrackedVar> activeVars;

    void insertTraceAfterStmt(Stmt* S) {
        SourceManager& SM = Context->getSourceManager();
        const LangOptions& LO = Context->getLangOpts();
        unsigned Line = SM.getSpellingLineNumber(S->getEndLoc());

        SourceLocation EndLoc = S->getEndLoc();
        // Проверяваме дали EndLoc вече е ';', ако не е — търсим ';' след него
        Token Tok;
        if (!Lexer::getRawToken(EndLoc, Tok, SM, LO, true) && Tok.is(tok::semi)) {
            EndLoc = Lexer::getLocForEndOfToken(EndLoc, 0, SM, LO);
        } else {
            EndLoc = Lexer::findLocationAfterToken(EndLoc, tok::semi, SM, LO, false);
        }

        if (EndLoc.isInvalid()) return;

        std::string Args;
        for (size_t i = 0; i < activeVars.size(); ++i) {
            const auto& v = activeVars[i];
            if (v.isArray) {
                Args += "TRACE_ARR(" + v.name + ", " + std::to_string(v.arraySize) + ")";
            } else if (v.isPointer) {
                Args += "TRACE_PTR(" + v.name + ")";
            } else {
                Args += "TRACE_VAR(" + v.name + ")";
            }
            if (i + 1 < activeVars.size()) Args += ", ";
        }

        std::string TraceCall = "\n    __trace_step(" + std::to_string(Line) + (Args.empty() ? "" : ", " + Args) + ");";
        TheRewriter.InsertTextAfter(EndLoc, TraceCall);
    }
};

class TraceASTConsumer : public ASTConsumer {
public:
    explicit TraceASTConsumer(ASTContext* Context, Rewriter& R)
        : Visitor(Context, R) {}

    void HandleTranslationUnit(ASTContext& Context) override {
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    }

private:
    TraceASTVisitor Visitor;
};

class TraceFrontendAction : public ASTFrontendAction {
public:
    void EndSourceFileAction() override {
        SourceManager& SM = TheRewriter.getSourceMgr();
        llvm::outs() << "#include \"tracer_runtime.hpp\"\n";
        TheRewriter.getEditBuffer(SM.getMainFileID()).write(llvm::outs());
    }

    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& CI, StringRef file) override {
        TheRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
        return std::make_unique<TraceASTConsumer>(&CI.getASTContext(), TheRewriter);
    }

private:
    Rewriter TheRewriter;
};

int main(int argc, const char** argv) {
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, TracerCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    ClangTool Tool(ExpectedParser->getCompilations(), ExpectedParser->getSourcePathList());
    return Tool.run(newFrontendActionFactory<TraceFrontendAction>().get());
}
