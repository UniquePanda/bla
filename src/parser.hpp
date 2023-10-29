#pragma once

#include <variant>
#include <vector>
#include <set>
#include <optional>

#include "utils.hpp"
#include "arenaAllocator.hpp"
#include "tokenizer.hpp"

struct ExprNode;

struct IntLitTermNode {
    Token int_lit;
};

struct DblLitTermNode {
    Token dbl_lit;
};

struct StrLitTermNode {
    Token str_lit;
};

struct BoolLitTermNode {
    Token bool_lit;
};

struct IdentTermNode {
    Token ident;
};

struct ParenTermNode {
    ExprNode* expr;
};

struct TermNode {
    std::variant<IdentTermNode*, IntLitTermNode*, DblLitTermNode*, StrLitTermNode*, BoolLitTermNode*, ParenTermNode*> var;
};

struct SumBinExprNode {
    ExprNode* lhs;
    ExprNode* rhs;
};

struct SubBinExprNode {
    ExprNode* lhs;
    ExprNode* rhs;
};

struct MulBinExprNode {
    ExprNode* lhs;
    ExprNode* rhs;
};

struct DivBinExprNode {
    ExprNode* lhs;
    ExprNode* rhs;
};

struct BinExprNode {
    std::variant<SumBinExprNode*, SubBinExprNode*, MulBinExprNode*, DivBinExprNode*> var;
};

struct UnExprNode {
    TermNode* term;
    std::vector<Token> operators;
};

struct ExprNode {
    std::variant<TermNode*, UnExprNode*, BinExprNode*> var;
};

struct StmtNode;

struct BuiltInFuncStmtNode {
    std::string funcName;
    ExprNode* expr;
};

struct LetStmtNode {
    Token ident;
    ExprNode* expr;
};

struct ScopeNode {
    std::vector<StmtNode*> stmts;
};

struct IfStmtNode {
    ExprNode* expr;
    ScopeNode* scope;
};

struct ElseIfStmtNode {
    ExprNode* expr;
    ScopeNode* scope;
};

struct ElseStmtNode {
    ScopeNode* scope;
};

struct StmtNode {
    std::variant<BuiltInFuncStmtNode*, LetStmtNode*, ScopeNode*, IfStmtNode*, ElseIfStmtNode*, ElseStmtNode*> var;
    size_t lineNumber = 0;
};

struct ProgNode {
    std::vector<StmtNode*> stmts;
};

class Parser {
public:
    Parser(std::vector<Token> tokens)
        : m_tokens(std::move(tokens)),
        m_allocator(1024 * 1024 * 4) // 4 MB
        {}

    std::optional<TermNode*> parseTerm() {
        m_lastStmt = {};

        if (!peek().has_value()) {
            return {};
        }

        if (peek().value().type == TokenType::int_lit) {
            auto intLitTerm = m_allocator.alloc<IntLitTermNode>();
            intLitTerm->int_lit = consume();
            auto term = m_allocator.alloc<TermNode>();
            term->var = intLitTerm;
            return term;
        } else if (peek().value().type == TokenType::dbl_lit) {
            auto dblLitTerm = m_allocator.alloc<DblLitTermNode>();
            dblLitTerm->dbl_lit = consume();
            auto term = m_allocator.alloc<TermNode>();
            term->var = dblLitTerm;
            return term;
        } else if (peek().value().type == TokenType::quot) {
            if (!peek(1).has_value() || peek(1).value().type != TokenType::str_lit) {
                failInvalidExpr("Quote must be followed by a string", m_lineNumber);
            }

            if (!peek(2).has_value() || peek(2).value().type != TokenType::quot) {
                failMissingQuot(m_lineNumber);
            }

            // Opening quotation mark.
            consume();

            auto strLitTerm = m_allocator.alloc<StrLitTermNode>();
            strLitTerm->str_lit = consume();
            auto term = m_allocator.alloc<TermNode>();
            term->var = strLitTerm;

            // Closing quotation mark.
            consume();

            return term;
        } else if (peek().value().type == TokenType::bool_lit) {
            auto boolLitTerm = m_allocator.alloc<BoolLitTermNode>();
            boolLitTerm->bool_lit = consume();
            auto term = m_allocator.alloc<TermNode>();
            term->var = boolLitTerm;
            return term;
        } else if (peek().value().type == TokenType::ident) {
            if (!m_idents.contains(peek().value().value.value())) {
                failUndeclaredIdentifer(peek().value().value.value(), m_lineNumber);
            }

            auto identTerm = m_allocator.alloc<IdentTermNode>();
            identTerm->ident = consume();
            auto term = m_allocator.alloc<TermNode>();
            term->var = identTerm;
            return term;
        } else if (peek().value().type == TokenType::open_paren) {
            // Opening parenthesis
            consume();

            auto expr = parseExpr();

            if (!expr.has_value()) {
                failMissingExpr("Parenthesis without expression", m_lineNumber);
            }

            if (!peek().has_value() || peek().value().type != TokenType::close_paren) {
                failMissingClosingParen(m_lineNumber);
            }
            // Closing parenthesis
            consume();

            auto parenTerm = m_allocator.alloc<ParenTermNode>();
            parenTerm->expr = expr.value();
            auto term = m_allocator.alloc<TermNode>();
            term->var = parenTerm;
            return term;
        } else {
            return {};
        }
    }

    template <typename T> T* parseSpecificBinExpr(ExprNode *lhsExpr, int precedence) {
        m_lastStmt = {};

        auto specificBinExpr = m_allocator.alloc<T>();
        specificBinExpr->lhs = lhsExpr;

        if (auto rhs = parseExpr(precedence + 1)) {
            specificBinExpr->rhs = rhs.value();
            return specificBinExpr;
        } else {
            failBinaryRHS(m_lineNumber);
        }

        return {};
    }

    std::optional<BinExprNode*> parseBinExpr(Token *operatorToken, ExprNode *lhsExpr) {
        m_lastStmt = {};

        auto binExpr = m_allocator.alloc<BinExprNode>();

        switch (operatorToken->type) {
            case TokenType::plus:
                binExpr->var = parseSpecificBinExpr<SumBinExprNode>(lhsExpr, binaryOperatorPrecedence(operatorToken->type));
                break;
            case TokenType::minus:
                binExpr->var = parseSpecificBinExpr<SubBinExprNode>(lhsExpr, binaryOperatorPrecedence(operatorToken->type));
                break;
            case TokenType::star:
                binExpr->var = parseSpecificBinExpr<MulBinExprNode>(lhsExpr, binaryOperatorPrecedence(operatorToken->type));
                break;
            case TokenType::slash:
                binExpr->var = parseSpecificBinExpr<DivBinExprNode>(lhsExpr, binaryOperatorPrecedence(operatorToken->type));
                break;
            default:
                failUnsupportedBinaryOperator(m_lineNumber);
        }

        return binExpr;
    }

    std::optional<ExprNode*> parseExpr(int minPrecedence = 0) {
        consumeLineBreaks();

        auto expr = m_allocator.alloc<ExprNode>();
        std::vector<Token> unaryOperators = {};

        int unOperatorPrecedence = unaryOperatorPrecedence(peek().value().type);

        while (unOperatorPrecedence < MAX_PRECEDENCE && unOperatorPrecedence >= minPrecedence) {
            unaryOperators.push_back(consume());
            consumeLineBreaks();
            unOperatorPrecedence = unaryOperatorPrecedence(peek().value().type);
        }
        bool hasUnaryOperators = unaryOperators.size() > 0;

        std::optional<TermNode*> lhsTerm = parseTerm();
        if (!lhsTerm.has_value()) {
            if (hasUnaryOperators) {
                failUexpectedUnaryOperator(unOpSymbol(unaryOperators.back().type), m_lineNumber);
            } else {
                return {};
            }
        }

        if (hasUnaryOperators && std::holds_alternative<StrLitTermNode*>(lhsTerm.value()->var)) {
            failUexpectedUnaryOperator(unOpSymbol(unaryOperators.back().type), m_lineNumber);
        }

        if (hasUnaryOperators) {
            auto unExpr = m_allocator.alloc<UnExprNode>();
            unExpr->operators = unaryOperators;
            unExpr->term = lhsTerm.value();
            expr->var = unExpr;
        } else {
            expr->var = lhsTerm.value();
        }

        consumeLineBreaks();

        while (peek().has_value()) {
            int binOperatorPrecedence = binaryOperatorPrecedence(peek().value().type);
            if (binOperatorPrecedence < minPrecedence || binOperatorPrecedence >= MAX_PRECEDENCE) {
                break;
            }

            Token operatorToken = consume();

            consumeLineBreaks();

            auto exprCopy = m_allocator.alloc<ExprNode>();
            exprCopy->var = expr->var;
            auto binExpr = parseBinExpr(&operatorToken, exprCopy);
            expr->var = binExpr.value();
        }

        return expr;
    }

    std::optional<ScopeNode*> parseScope() {
        // Don't set m_lastStmt here, because scopes are often used between other statements which might depend on each other.
        if (!peek().has_value()) {
            return {};
        }

        if (peek().value().type != TokenType::open_curly) {
            failMissingOpenCurly(m_lineNumber);
        }

        if (!peek(1).has_value()) {
            failUnexpectedEOF(m_lineNumber);
        }

        // Open curly
        consume();

        consumeLineBreaks();

        auto scopeStmt = m_allocator.alloc<ScopeNode>();

        auto stmt = parseStmt();
        while (stmt) {
            scopeStmt->stmts.push_back(stmt.value());

            consumeLineBreaks();

            stmt = parseStmt();
        }

        if (!peek().has_value()) {
            failUnexpectedEOF(m_lineNumber);
        }

        if (peek().value().type != TokenType::close_curly) {
            failMissingClosingCurly(m_lineNumber);
        }

        // Closing curly
        consume();

        return scopeStmt;
    }

    std::optional<StmtNode*> parseStmt() {
        if (!peek().has_value()) {
            return {};
        }

        if (peek().value().type == TokenType::built_in_func) {
            if (!peek(1).has_value()) {
                failUnexpectedEOF(m_lineNumber);
            }

            if (peek(1).value().type != TokenType::open_paren) {
                failMissingOpenParen(m_lineNumber);
            }

            auto builtInFuncStmt = m_allocator.alloc<BuiltInFuncStmtNode>();
            auto funcName = peek().value().value.value();
            builtInFuncStmt->funcName = funcName;

            // function name and opening parenthesis
            consume();
            consume();

            consumeLineBreaks();

            if (auto exprNode = parseExpr()) {
                builtInFuncStmt->expr = exprNode.value();
            } else {
                failInvalidExpr("Unknown expression as function parameter", m_lineNumber);
            }

            consumeLineBreaks();

            if (peek().has_value() && peek().value().type == TokenType::close_paren) {
                consume();
            } else {
                failMissingClosingParen(m_lineNumber);
            }

            consumeSemi();

            if (builtInFuncStmt->funcName == "exit" && peek().has_value()) {
                std::cerr << "Found code after exit()" << std::endl;
                exit(EXIT_FAILURE);
            }

            m_lastStmt = TokenType::built_in_func;

            auto stmt = m_allocator.alloc<StmtNode>();
            stmt->var = builtInFuncStmt;
            return stmt;
        } else if (peek().value().type == TokenType::let) {
            if (!peek(1).has_value()) {
                failUnexpectedEOF(m_lineNumber);
            }

            if (peek(1).value().type != TokenType::ident) {
                failMissingIdent("let statement without identifier", m_lineNumber);
            }

            // let
            consume();

            // ident
            auto letStmt = m_allocator.alloc<LetStmtNode>();
            letStmt->ident = consume();

            consumeLineBreaks();

            if (peek().value().type != TokenType::eq) {
                failMissingOperator("let statement without equal sign", m_lineNumber);
            }

            // equal sign
            consume();

            consumeLineBreaks();

            if (auto expr = parseExpr()) {
                letStmt->expr = expr.value();
            } else {
                failInvalidExpr("Unknown expression as value in let statement", m_lineNumber);
            }

            consumeSemi();

            m_lastStmt = TokenType::let;

            auto stmt = m_allocator.alloc<StmtNode>();
            stmt->var = letStmt;

            m_idents.insert(letStmt->ident.value.value());

            return stmt;
        } else if (peek().value().type == TokenType::open_curly) {
            // Don't set m_lastStmt here, because scopes are often used between other statements which might depend on each other.
            if (auto scopeStmt = parseScope()) {
                auto stmt = m_allocator.alloc<StmtNode>();
                stmt->var = scopeStmt.value();
                return stmt;
            } else {
                failInvalidScope(m_lineNumber);
            }
        } else if (peek().value().type == TokenType::if_) {
            if (!peek(1).has_value()) {
                failUnexpectedEOF(m_lineNumber);
            }

            if (peek(1).value().type != TokenType::open_paren) {
                failMissingOpenParen(m_lineNumber);
            }

            // if and open parenthesis
            consume();
            consume();

            auto ifStmt = m_allocator.alloc<IfStmtNode>();

            if (auto expr = parseExpr()) {
                ifStmt->expr = expr.value();
            } else {
                failMissingExpr("If statement without expression", m_lineNumber);
            }

            if (peek().value().type != TokenType::close_paren) {
                failMissingClosingParen(m_lineNumber);
            }

            // Closing parenthesis
            consume();

            if (auto scopeStmt = parseScope()) {
                ifStmt->scope = scopeStmt.value();
            } else {
                failInvalidScope(m_lineNumber);
            }

            m_lastStmt = TokenType::if_;

            auto stmt = m_allocator.alloc<StmtNode>();
            stmt->var = ifStmt;
            return stmt;
        } else if (peek().value().type == TokenType::elseif) {
            if (m_lastStmt != TokenType::if_ && m_lastStmt != TokenType::elseif) {
                failMissingStmt("if", m_lineNumber);
            }

            if (!peek(1).has_value()) {
                failUnexpectedEOF(m_lineNumber);
            }

            if (peek(1).value().type != TokenType::open_paren) {
                failMissingOpenParen(m_lineNumber);
            }

            // elseif and open parenthesis
            consume();
            consume();

            auto elseIfStmt = m_allocator.alloc<ElseIfStmtNode>();

            if (auto expr = parseExpr()) {
                elseIfStmt->expr = expr.value();
            } else {
                failMissingExpr("Elseif statement without expression", m_lineNumber);
            }

            if (peek().value().type != TokenType::close_paren) {
                failMissingClosingParen(m_lineNumber);
            }

            // Closing parenthesis
            consume();

            if (auto scopeStmt = parseScope()) {
                elseIfStmt->scope = scopeStmt.value();
            } else {
                failInvalidScope(m_lineNumber);
            }

            m_lastStmt = TokenType::elseif;

            auto stmt = m_allocator.alloc<StmtNode>();
            stmt->var = elseIfStmt;
            return stmt;
        } else if (peek().value().type == TokenType::else_) {
            if (m_lastStmt != TokenType::if_ && m_lastStmt != TokenType::elseif) {
                failMissingStmt("if", m_lineNumber);
            }

            if (!peek(1).has_value()) {
                failUnexpectedEOF(m_lineNumber);
            }

            // else
            consume();

            auto elseStmt = m_allocator.alloc<ElseStmtNode>();

            if (auto scopeStmt = parseScope()) {
                elseStmt->scope = scopeStmt.value();
            } else {
                failInvalidScope(m_lineNumber);
            }

            m_lastStmt = TokenType::else_;

            auto stmt = m_allocator.alloc<StmtNode>();
            stmt->var = elseStmt;
            return stmt;
        } else {
            return {};
        }

        return {};
    }

    std::optional<ProgNode> parseProg() {
        ProgNode prog;
        while (peek().has_value()) {
            if (peek().value().type == TokenType::line_break) {
                m_lineNumber++;
                consume();
            } else if (auto stmt = parseStmt()) {
                stmt.value()->lineNumber = m_lineNumber;
                prog.stmts.push_back(stmt.value());
            } else {
                failInvalidStmt(m_lineNumber);
            }
        }

        std::cout << "      ## Parser used " << m_allocator.usedSizeInBytes() << " Bytes." << std::endl;

        return prog;
    }

private:
    static const int MAX_PRECEDENCE = 3;

    [[nodiscard]] std::optional<Token> peek(int offset = 0) const {
        if (m_index + offset >= m_tokens.size()) {
            return {};
        } else {
            return m_tokens.at(m_index + offset);
        }
    }

    Token consume() {
        return m_tokens.at(m_index++);
    }

    void consumeSemi() {
        // Always allow line breaks before semicolons.
        consumeLineBreaks();

        if (peek().has_value() && peek().value().type == TokenType::semi) {
            consume();
        } else {
            failMissingSemi(m_lineNumber);
        }
    }

    void consumeLineBreaks() {
        while (peek().value().type == TokenType::line_break) {
            m_lineNumber++;
            consume();
        }
    }

    int unaryOperatorPrecedence(TokenType operatorType) {
        switch (operatorType) {
            case TokenType::plus:
            case TokenType::minus:
                return 2;

            default:
                return MAX_PRECEDENCE;
        }
    }

    int binaryOperatorPrecedence(TokenType operatorType) {
        switch (operatorType) {
            case TokenType::plus:
            case TokenType::minus:
                return 0;

            case TokenType::star:
            case TokenType::slash:
                return 1;

            default:
                return MAX_PRECEDENCE;
        }
    }

    const std::vector<Token> m_tokens;
    size_t m_index = 0;
    size_t m_lineNumber = 1;
    TokenType m_lastStmt = {};
    std::set<std::string> m_idents {};
    ArenaAllocator m_allocator;
};
