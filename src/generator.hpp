#pragma once

#include <algorithm>
#include <cassert>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <map>
#include <variant>

#include "utils.hpp"
#include "parser.hpp"

class Generator {
public:
    Generator(ProgNode prog) : m_prog(std::move(prog)) {}

    TokenType generateTerm(const TermNode* term, const std::vector<Token> unaryOperators = {}, bool isReassignment = false) {
        struct TermVisitor {
            Generator& generator;
            const std::vector<Token>& unaryOperators;
            bool& isReassignment;

            TokenType operator()(const IntLitTermNode* intLitTerm) const {
                std::string valueString = negUnOperatorCondensed(unaryOperators) + intLitTerm->int_lit.value.value();
                generator.m_codeSectionAsmOutput << "    mov rax, " << valueString << "\n";

                if (!isReassignment) {
                    generator.pushInternalIntVar("rax");
                }

                return TokenType::int_lit;
            }

            TokenType operator()(const DblLitTermNode* dblLitTerm) const {
                std::string valueString = negUnOperatorCondensed(unaryOperators) + dblLitTerm->dbl_lit.value.value();
                auto dataLoc = generator.addDoubleToDataSection(std::stod(valueString));

                generator.m_codeSectionAsmOutput << "    mov rax, dbl" << dataLoc << "\n";

                if (!isReassignment) {
                    generator.pushConst(
                        "rax",
                        generator.internalConstIdent(dataLoc, "DBL"),
                        TokenType::dbl_lit,
                        dataLoc,
                        valueString
                    );
                }

                return TokenType::dbl_lit;
            }

            TokenType operator()(const StrLitTermNode* strLitTerm) const {
                if (unaryOperators.size() > 0) {
                    failUexpectedUnaryOperator(unOpSymbol(unaryOperators.back().type), generator.m_lineNumber);
                }

                auto dataLoc = addStringToDataSection(strLitTerm->str_lit.value.value());

                generator.m_codeSectionAsmOutput << "    mov rax, string" << dataLoc << "\n";

                if (!isReassignment) {
                    generator.pushConst(
                        "rax",
                        generator.internalConstIdent(dataLoc, "STRING"),
                        TokenType::str_lit,
                        dataLoc,
                        strLitTerm->str_lit.value.value()
                    );
                }

                return TokenType::str_lit;
            }

            TokenType operator()(const BoolLitTermNode* boolLitTerm) const {
                // + and - turn bool into an integer
                bool hasUnPlusMinus = false;
                for (Token unOperator : unaryOperators) {
                    if (unOperator.type == TokenType::plus || unOperator.type == TokenType::minus) {
                        hasUnPlusMinus = true;
                        break;
                    }
                }

                std::string valueString = (hasUnPlusMinus ? negUnOperatorCondensed(unaryOperators) : "") + (boolLitTerm->bool_lit.value.value() == "true" ? "1" : "0");
                generator.m_codeSectionAsmOutput << "    mov rax, " << valueString << "\n";

                if (!isReassignment) {
                    if (hasUnPlusMinus) {
                        generator.pushInternalIntVar("rax");
                    } else {
                        generator.pushInternalBoolVar("rax");
                    }
                }

                if (hasUnPlusMinus) {
                    return TokenType::int_lit;
                } else {
                    return TokenType::bool_lit;
                }
            }

            TokenType operator()(const IdentTermNode* identTerm) const {
                auto identName = identTerm->ident.value.value();
                bool isConst = generator.m_consts.contains(identName);
                if (!isConst && !generator.m_vars.contains(identName)) {
                    failUndeclaredIdentifer(identName, generator.m_lineNumber);
                }

                if (isConst) {
                    const auto& cons = generator.m_consts.at(identName);
                    std::string typeIdent = "";
                    if (cons.type == TokenType::str_lit) {
                        if (unaryOperators.size() > 0) {
                            failUexpectedUnaryOperator(unOpSymbol(unaryOperators.back().type), generator.m_lineNumber);
                        }

                        typeIdent = "string";
                    } else if (cons.type == TokenType::dbl_lit) {
                        typeIdent = "dbl";
                    }

                    if (cons.type == TokenType::dbl_lit && negUnOperatorCondensed(unaryOperators) == "-") {
                        // Store negated value of the double.
                        std::string valueString = "-" + generator.m_consts.at(identName).valueAtDataLoc;
                        auto dataLoc = generator.addDoubleToDataSection(std::stod(valueString));

                        generator.m_codeSectionAsmOutput << "    mov rax, dbl" << dataLoc << "\n";
                        generator.pushConst(
                            "rax",
                            generator.internalConstIdent(dataLoc, "DBL"),
                            TokenType::dbl_lit,
                            dataLoc,
                            valueString
                        );
                        return TokenType::dbl_lit;
                    } else {
                        std::stringstream stackLocation;
                        stackLocation << "QWORD [rsp + " << (generator.m_stackSize - cons.stackLoc - 1) * 8 << "]";
                        generator.m_codeSectionAsmOutput << "    mov rax, " << stackLocation.str() << "\n";
                        generator.pushConst("rax", identName);
                        return cons.type;
                    }
                } else {
                    const auto& var = generator.m_vars.at(identName);

                    if (var.type == TokenType::str_lit && unaryOperators.size() > 0) {
                        failUexpectedUnaryOperator(unOpSymbol(unaryOperators.back().type), generator.m_lineNumber);
                    }

                    bool hasUnPlusMinus = false;
                    for (Token unOperator : unaryOperators) {
                        if (unOperator.type == TokenType::plus || unOperator.type == TokenType::minus) {
                            hasUnPlusMinus = true;
                            break;
                        }
                    }

                    bool isNegated = negUnOperatorCondensed(unaryOperators) == "-";

                    std::stringstream stackLocation;
                    stackLocation << "QWORD [rsp + " << (generator.m_stackSize - var.stackLoc - 1) * 8 << "]";

                    if (isNegated) {
                        if (var.type == TokenType::int_lit || var.type == TokenType::bool_lit) {
                            generator.m_codeSectionAsmOutput << "    mov rax, " << stackLocation.str() << "\n";
                            generator.m_codeSectionAsmOutput << "    neg rax" << "\n";
                            generator.pushInternalIntVar("rax");
                            return TokenType::int_lit;
                        } else if (var.type == TokenType::dbl_lit) {
                            generator.m_codeSectionAsmOutput << "    movq xmm0, " << stackLocation.str() << "\n";
                            generator.m_codeSectionAsmOutput << "    movq xmm1, [FLIP_S_64B]\n";
                            generator.m_codeSectionAsmOutput << "    xorps xmm0, xmm1\n";
                            generator.pushInternalDblVar("xmm0");
                            return TokenType::dbl_lit;
                        } else {
                            failUexpectedUnaryOperator(unOpSymbol(unaryOperators.back().type), generator.m_lineNumber);
                        }
                    } else if (hasUnPlusMinus && var.type == TokenType::bool_lit) {
                        generator.m_codeSectionAsmOutput << "    mov rax, " << stackLocation.str() << "\n";
                        generator.pushInternalIntVar("rax");
                        return TokenType::int_lit;
                    } else {
                        generator.pushVar(stackLocation.str(), identName, var.type);
                        return var.type;
                    }

                    // Should never happen.
                    throw std::invalid_argument("TermVisitor for IdentTerm didn't reach return value. This should never happen.");
                }
            }

            TokenType operator()(const ParenTermNode* parenTerm) const {
                return generator.generateExpr(parenTerm->expr, unaryOperators);
            }

            size_t addStringToDataSection(std::string str) const {
                std::vector<std::string> subStrings;

                size_t singleQuotPos = 0;
                while ((singleQuotPos = str.find("'")) != std::string::npos) {
                    subStrings.push_back(str.substr(0, singleQuotPos));
                    str.erase(0, singleQuotPos + 1);
                }
                subStrings.push_back(str);

                generator.m_dataSectionAsmOutput << "    string" << generator.m_dataSize << " db ";
                for (int i = 0; i < subStrings.size(); i++) {
                    generator.m_dataSectionAsmOutput << "'" << subStrings.at(i) << "'";

                    if (i < subStrings.size() - 1) {
                        generator.m_dataSectionAsmOutput << ", \"'\", ";
                    }
                }
                generator.m_dataSectionAsmOutput << ", 0\n";
                generator.m_dataSize++;

                return generator.m_dataSize - 1;
            }
        };

        TermVisitor visitor({ .generator = *this, .unaryOperators = unaryOperators, .isReassignment = isReassignment });
        return std::visit(visitor, term->var);
    }

    TokenType generateExpr(const ExprNode* expr, const std::vector<Token> unaryOperators = {}, bool isReassignment = false) {
        struct BinExprVisitor {
            Generator& generator;

            TokenType operator()(const SumBinExprNode* sumBinExpr) const {
                generator.generateExpr(sumBinExpr->lhs);
                generator.generateExpr(sumBinExpr->rhs);

                generator.popWithConstNoStrLit("rax", "xmm0");
                generator.popWithConstNoStrLit("rbx", "xmm1");

                bool shouldPushDbl = true;

                if (!generator.m_wasSecondLastPopDbl && !generator.m_wasLastPopDbl) {
                    // Int + Int
                    generator.m_codeSectionAsmOutput << "    add rax, rbx\n";
                    shouldPushDbl = false;
                } else if (generator.m_wasSecondLastPopDbl && !generator.m_wasLastPopDbl) {
                    // Int + Dbl
                    generator.m_codeSectionAsmOutput << "    cvtsi2sd xmm1, rbx\n";
                    generator.m_codeSectionAsmOutput << "    addsd xmm0, xmm1\n";
                } else if (!generator.m_wasSecondLastPopDbl && generator.m_wasLastPopDbl) {
                    // Dbl + Int
                    generator.m_codeSectionAsmOutput << "    cvtsi2sd xmm0, rax\n";
                    generator.m_codeSectionAsmOutput << "    addsd xmm0, xmm1\n";
                } else if (generator.m_wasSecondLastPopDbl && generator.m_wasLastPopDbl) {
                    // Dbl + Dbl
                    generator.m_codeSectionAsmOutput << "    addsd xmm0, xmm1\n";
                }

                return pushResult(shouldPushDbl);
            }

            TokenType operator()(const SubBinExprNode* subBinExpr) const {
                generator.generateExpr(subBinExpr->lhs);
                generator.generateExpr(subBinExpr->rhs);

                generator.popWithConstNoStrLit("rbx", "xmm1");
                generator.popWithConstNoStrLit("rax", "xmm0");

                bool shouldPushDbl = true;

                if (!generator.m_wasSecondLastPopDbl && !generator.m_wasLastPopDbl) {
                    // Int - Int
                    generator.m_codeSectionAsmOutput << "    sub rax, rbx\n";
                    shouldPushDbl = false;
                } else if (generator.m_wasSecondLastPopDbl && !generator.m_wasLastPopDbl) {
                    // Int - Dbl
                    generator.m_codeSectionAsmOutput << "    cvtsi2sd xmm0, rax\n";
                    generator.m_codeSectionAsmOutput << "    subsd xmm0, xmm1\n";
                } else if (!generator.m_wasSecondLastPopDbl && generator.m_wasLastPopDbl) {
                    // Dbl - Int
                    generator.m_codeSectionAsmOutput << "    cvtsi2sd xmm1, rbx\n";
                    generator.m_codeSectionAsmOutput << "    subsd xmm0, xmm1\n";
                } else if (generator.m_wasSecondLastPopDbl && generator.m_wasLastPopDbl) {
                    // Dbl - Dbl
                    generator.m_codeSectionAsmOutput << "    subsd xmm0, xmm1\n";
                }

                return pushResult(shouldPushDbl);
            }

            TokenType operator()(const MulBinExprNode* mulBinExpr) const {
                generator.generateExpr(mulBinExpr->lhs);
                generator.generateExpr(mulBinExpr->rhs);

                generator.popWithConstNoStrLit("rax", "xmm0");
                generator.popWithConstNoStrLit("rbx", "xmm1");

                bool shouldPushDbl = true;

                if (!generator.m_wasSecondLastPopDbl && !generator.m_wasLastPopDbl) {
                    // Int * Int
                    generator.m_codeSectionAsmOutput << "    imul rax, rbx\n";
                    shouldPushDbl = false;
                } else if (generator.m_wasSecondLastPopDbl && !generator.m_wasLastPopDbl) {
                    // Int * Dbl
                    generator.m_codeSectionAsmOutput << "    cvtsi2sd xmm1, rbx\n";
                    generator.m_codeSectionAsmOutput << "    mulsd xmm0, xmm1\n";
                } else if (!generator.m_wasSecondLastPopDbl && generator.m_wasLastPopDbl) {
                    // Dbl * Int
                    generator.m_codeSectionAsmOutput << "    cvtsi2sd xmm0, rax\n";
                    generator.m_codeSectionAsmOutput << "    mulsd xmm0, xmm1\n";
                } else if (generator.m_wasSecondLastPopDbl && generator.m_wasLastPopDbl) {
                    // Dbl * Dbl
                    generator.m_codeSectionAsmOutput << "    mulsd xmm0, xmm1\n";
                }

                return pushResult(shouldPushDbl);
            }

            TokenType operator()(const DivBinExprNode* divBinExpr) const {
                generator.generateExpr(divBinExpr->lhs);
                generator.generateExpr(divBinExpr->rhs);

                generator.popWithConstNoStrLit("rbx", "xmm1"); // Divisor
                generator.popWithConstNoStrLit("rax", "xmm0"); // Divident

                bool shouldPushDbl = true;

                if (!generator.m_wasSecondLastPopDbl && !generator.m_wasLastPopDbl) {
                    // Int / Int
                    generator.m_codeSectionAsmOutput << "    cqo\n";
                    generator.m_codeSectionAsmOutput << "    idiv rbx\n";
                    shouldPushDbl = false;
                } else if (generator.m_wasSecondLastPopDbl && !generator.m_wasLastPopDbl) {
                    // Int / Dbl
                    generator.m_codeSectionAsmOutput << "    cvtsi2sd xmm0, rax\n";
                    generator.m_codeSectionAsmOutput << "    DIVSD xmm0, xmm1\n";
                } else if (!generator.m_wasSecondLastPopDbl && generator.m_wasLastPopDbl) {
                    // Dbl / Int
                    generator.m_codeSectionAsmOutput << "    cvtsi2sd xmm1, rbx\n";
                    generator.m_codeSectionAsmOutput << "    DIVSD xmm0, xmm1\n";
                } else if (generator.m_wasSecondLastPopDbl && generator.m_wasLastPopDbl) {
                    // Dbl / Dbl
                    generator.m_codeSectionAsmOutput << "    DIVSD xmm0, xmm1\n";
                }

                return pushResult(shouldPushDbl);
            }

            TokenType pushResult(const bool& shouldPushDbl) const {
                if (shouldPushDbl) {
                    generator.pushInternalDblVar("xmm0");
                    return TokenType::dbl_lit;
                } else {
                    generator.pushInternalIntVar("rax");
                    return TokenType::int_lit;
                }
            }
        };

        struct ExprVisitor {
            Generator& generator;
            bool& isReassignment;

            TokenType operator()(const TermNode* term) const {
                return generator.generateTerm(term, {}, isReassignment);
            }

            TokenType operator()(const BinExprNode* binExpr) const {
                BinExprVisitor visitor({.generator = generator});
                return std::visit(visitor, binExpr->var);
            }

            TokenType operator()(const UnExprNode* unExpr) const {
                return generator.generateTerm(unExpr->term, unExpr->operators, isReassignment);
            }
        };

        ExprVisitor visitor({.generator = *this, .isReassignment = isReassignment});
        return std::visit(visitor, expr->var);
    }

    void generateScope(const ScopeNode* scope) {
        beginScope();

        for (const StmtNode* stmt : scope->stmts) {
            generateStmt(stmt);
        }

        endScope();
    }

    void generateStmt(const StmtNode* stmt) {
        struct StmtVisitor {
            Generator& generator;

            void operator()(const BuiltInFuncStmtNode* builtInFuncStmt) const {
                if (builtInFuncStmt->funcName != "asm") {
                    generator.generateExpr(builtInFuncStmt->expr);
                }

                if (builtInFuncStmt->funcName == "exit") {
                    generator.m_codeSectionAsmOutput << "    mov rax, 60\n"; // sys_exit
                    generator.popWithConstOnlyInt("rdi"); // exit code
                    generator.m_codeSectionAsmOutput << "    syscall\n\n";

                    generator.m_containsCustomExitCall = true;
                } else if (builtInFuncStmt->funcName == "print" || builtInFuncStmt->funcName == "println") {
                    bool isInt = true; // If nothing else can be determined, just assume int.
                    bool isDouble = false;
                    bool isConst = false;

                    if (generator.m_werePushesDouble.back()) {
                        const auto popInfo = generator.pop("xmm0");
                        isInt = false;
                        isDouble = true;
                        generator.m_codeSectionAsmOutput << "    xor rdi, rdi\n"; // Makes sure "dtoa" is reading double directly from xmm0
                    } else if (const auto popInfo = generator.pop("rdi")) {
                        std::string identName = std::get<0>(popInfo.value());
                        isConst = std::get<1>(popInfo.value());
                        TokenType type = std::get<2>(popInfo.value());

                        if (isConst) {
                            if (type == TokenType::int_lit) {
                                generator.m_codeSectionAsmOutput << "    mov rdi, [rdi]\n"; // Just move number into rdi as input for itoa
                            } else if (type == TokenType::dbl_lit) {
                                isInt = false;
                                isDouble = true;
                            } else if (type == TokenType::str_lit) {
                                isInt = false;
                                generator.m_codeSectionAsmOutput << "    mov rsi, rdi\n";
                                generator.m_codeSectionAsmOutput << "    mov rdx, " << generator.m_consts.at(identName).valueLength << "\n";
                            } else if (type == TokenType::bool_lit) {
                                isInt = false;
                                generator.m_codeSectionAsmOutput << "    call movBoolStr\n"; // rsi points to bool string ("true"/"false"), rdx has string length
                            } else {
                                failUnknownDataType(generator.m_lineNumber);
                            }
                        } else { // If not a const, rdi will hold the value directly
                            if (type == TokenType::bool_lit) {
                                isInt = false;
                                generator.m_codeSectionAsmOutput << "    call movBoolStr\n"; // rsi points to bool string ("true"/"false"), rdx has string length
                            } else if (type == TokenType::dbl_lit) { // rdi contains double
                                isInt = false;
                                isDouble = true;
                                generator.m_codeSectionAsmOutput << "    mov [TEMP_DBL], rdi\n";
                                generator.m_codeSectionAsmOutput << "    mov rdi, TEMP_DBL\n";
                            } else if (type != TokenType::int_lit) { // If it's an int, the value is already in rdi
                                failUnknownDataType(generator.m_lineNumber);
                            }
                        }
                    }

                    if (isInt) {
                        generator.m_codeSectionAsmOutput << "    call itoa\n"; // rsi points to first char, rdx has string length
                    }

                    if (isDouble) {
                        generator.m_codeSectionAsmOutput << "    call dtoa\n"; // rsi points to first char, rdx has string length
                    }

                    if (builtInFuncStmt->funcName == "println") {
                        // Move rsi before the generated string, add a linebreak and move rsi back. As the string is read
                        // from back to front, this will actually put the linebreak behind the last character.
                        generator.m_codeSectionAsmOutput << "    add rsi, rdx\n";

                        // If we are dealing with a constant, we potentially override important stuff if we add an additional
                        // char. In this case, store the original value and restore it afterwards.
                        // TODO: Probably dangerous? :D
                        if (isConst) {
                            generator.m_codeSectionAsmOutput << "    mov r8b, [rsi]\n";
                        }

                        generator.m_codeSectionAsmOutput << "    mov BYTE [rsi], 0xa\n";
                        generator.m_codeSectionAsmOutput << "    sub rsi, rdx\n";
                        generator.m_codeSectionAsmOutput << "    inc edx\n"; // increase number of chars to read by 1
                    }

                    if (isInt || isDouble) {
                        // Move the stack pointer so the next syscall won't override the string.
                        generator.m_codeSectionAsmOutput << "    sub rsp, rdx\n";
                        generator.m_codeSectionAsmOutput << "    sub rsp, 8\n";
                    }

                    // Perform the stdout write.
                    generator.m_codeSectionAsmOutput << "    mov rdi, 1\n"; // file descriptor 1 (stdout) for sys_write syscall
                    generator.m_codeSectionAsmOutput << "    call sysWrite\n";

                    if (isInt || isDouble) {
                        // Reset stack pointer.
                        generator.m_codeSectionAsmOutput << "    add rsp, 8\n";
                        generator.m_codeSectionAsmOutput << "    add rsp, rdx\n";
                    }

                    if (builtInFuncStmt->funcName == "println" && isConst) {
                        // Restore value before our string
                        generator.m_codeSectionAsmOutput << "    dec edx\n";
                        generator.m_codeSectionAsmOutput << "    add rsi, rdx\n";
                        generator.m_codeSectionAsmOutput << "    mov [rsi], r8b\n";
                    }
                } else if (builtInFuncStmt->funcName == "asm") {
                    // We know that the statement contains a TermNode with a string literal because the parser is already checking that.
                    TermNode* term = std::get<TermNode*>(builtInFuncStmt->expr->var);
                    std::string str = std::get<StrLitTermNode*>(term->var)->str_lit.value.value();

                    generator.m_codeSectionAsmOutput << "    " << str << "\n";
                } else {
                    failUnknownBuiltInFunc(builtInFuncStmt->funcName, generator.m_lineNumber);
                }
            }

            void operator()(const LetStmtNode* letStmt) const {
                if (
                    generator.m_vars.contains(letStmt->ident.value.value())
                    || generator.m_consts.contains(letStmt->ident.value.value())
                ) {
                    failAlreadyUsedIdentifer(letStmt->ident.value.value(), generator.m_lineNumber);
                }

                std::string ident = letStmt->ident.value.value();
                auto expr = letStmt->expr;

                generator.assignment(ident, expr);
            }

            void operator()(const ReassignStmtNode* reassignStmt) const {
                if (
                    !generator.m_vars.contains(reassignStmt->ident.value.value())
                    && !generator.m_consts.contains(reassignStmt->ident.value.value())
                ) {
                    failUndeclaredIdentifer(reassignStmt->ident.value.value(), generator.m_lineNumber);
                }

                std::string ident = reassignStmt->ident.value.value();
                auto expr = reassignStmt->expr;

                generator.assignment(ident, expr, ident);
            }

            void operator()(const ScopeNode* scope) const {
                generator.generateScope(scope);
            }

            void operator()(const IfStmtNode* ifStmt) const {
                generator.generateExpr(ifStmt->expr);

                std::string label = generator.getLabelName();

                if (generator.m_werePushesDouble.back()) {
                    generator.pop("xmm0");
                    generator.m_codeSectionAsmOutput << "    ptest xmm0, xmm0" << "\n";
                    generator.m_codeSectionAsmOutput << "    jz " << label + "_setFalse" << "\n";
                } else {
                    generator.pop("rax");
                    generator.m_codeSectionAsmOutput << "    test rax, rax" << "\n";
                    generator.m_codeSectionAsmOutput << "    jz " << label + "_setFalse" << "\n";
                }

                generator.pushIfStmtBoolVar("1", label); // Mark this label as "true"
                generator.generateScope(ifStmt->scope);
                generator.m_codeSectionAsmOutput << "    jmp " << label << "\n";

                generator.m_codeSectionAsmOutput << "    " << label + "_setFalse" << ":\n";
                generator.pushIfStmtBoolVar("0", label, false); // Mark this label as "false"
                generator.m_codeSectionAsmOutput << "    " << label << ":\n";
            }

            void operator()(const ElseIfStmtNode* elseIfStmt) const {
                std::string label = generator.getLabelName();

                // Last value on stack is always the push of the if or elseif statement result, because either nothing
                // was done in the if statement (else executed directly) or a scope was done (stack was cleaned afterwards).
                generator.pop("rax");
                generator.m_codeSectionAsmOutput << "    test rax, rax" << "\n";
                generator.m_codeSectionAsmOutput << "    jnz " << label + "_setTrue" << "\n";

                generator.generateExpr(elseIfStmt->expr);

                if (generator.m_werePushesDouble.back()) {
                    generator.pop("xmm0");
                    generator.m_codeSectionAsmOutput << "    ptest xmm0, xmm0" << "\n";
                    generator.m_codeSectionAsmOutput << "    jz " << label + "_setFalse" << "\n";
                } else {
                    generator.pop("rax");
                    generator.m_codeSectionAsmOutput << "    test rax, rax" << "\n";
                    generator.m_codeSectionAsmOutput << "    jz " << label + "_setFalse" << "\n";
                }

                generator.pushIfStmtBoolVar("1", label); // Mark this label as "true"
                generator.generateScope(elseIfStmt->scope);
                generator.m_codeSectionAsmOutput << "    jmp " << label << "\n";

                generator.m_codeSectionAsmOutput << "    " << label + "_setTrue" << ":\n";
                generator.pushIfStmtBoolVar("1", label, false); // Mark this label as "true"
                generator.m_codeSectionAsmOutput << "    jmp " << label << "\n";

                generator.m_codeSectionAsmOutput << "    " << label + "_setFalse" << ":\n";
                generator.pushIfStmtBoolVar("0", label, false); // Mark this label as "false"
                generator.m_codeSectionAsmOutput << "    " << label << ":\n";
            }

            void operator()(const ElseStmtNode* elseStmt) const {
                std::string label = generator.getLabelName();

                // Last value on stack is always the push of the if or elseif statement result, because either nothing
                // was done in the if statement (else executed directly) or a scope was done (stack was cleaned afterwards).
                generator.pop("rax");
                generator.m_codeSectionAsmOutput << "    test rax, rax" << "\n";
                generator.m_codeSectionAsmOutput << "    jnz " << label << "\n";

                generator.generateScope(elseStmt->scope);

                generator.m_codeSectionAsmOutput << "    " << label << ":\n";
            }
        };

        StmtVisitor visitor({ .generator = *this });
        std::visit(visitor, stmt->var);
    }

    [[nodiscard]] std::string generateProg() {
        m_dataSectionAsmOutput << "section .bss\n";
        m_dataSectionAsmOutput << "    TEMP_DBL resq 1\n";
        m_dataSectionAsmOutput << "\n\n";

        m_dataSectionAsmOutput << "section .data\n";
        m_dataSectionAsmOutput << "    STRING_TRUE db 'true', 0\n";
        m_dataSectionAsmOutput << "    STRING_FALSE db 'false', 0\n";
        m_dataSectionAsmOutput << "    FLIP_S_64B dq 0x8000000000000000\n";
        m_dataSectionAsmOutput << "    MUL_FRAC_TO_INT dq 1000000000000000.0\n";
        m_dataSectionAsmOutput << "    DBL_0_1 dq 0.1\n";
        m_dataSectionAsmOutput << "    DBL_1_0 dq 1.0\n";
        m_dataSectionAsmOutput << "\n";

        m_codeSectionAsmOutput << "section .text\n";
        m_codeSectionAsmOutput << "global _start\n";
        m_codeSectionAsmOutput << "_start:\n";

        for (const StmtNode* stmt : m_prog.stmts) {
            m_lineNumber = stmt->lineNumber;
            generateStmt(stmt);
        }

        if (!m_containsCustomExitCall) {
            m_codeSectionAsmOutput << "    mov rax, 60\n";
            m_codeSectionAsmOutput << "    mov rdi, 0\n";
            m_codeSectionAsmOutput << "    syscall\n\n";
        }

        addIntToAsciiAsm();
        addDoubleToAsciiAsm();
        addCharacterCountAsm();
        addMovBoolStrAsm();
        addSysWriteAsm();

        m_asmOutput << m_dataSectionAsmOutput.str() << "\n\n" << m_codeSectionAsmOutput.str();

        return m_asmOutput.str();
    }

private:
    void pushVar(
        const std::string& regOrValue,
        const std::string& identName = "",
        const TokenType& type = {},
        const bool& shouldIncreaseStackSize = true
    ) {
        if (shouldIncreaseStackSize) {
            if (!m_vars.contains(identName)) {
                m_vars.insert({ identName, Var {.stackLoc = m_stackSize, .type = type}});
                m_varsInOrder.push_back(identName);
            }

            m_varsOnStack.insert({ m_stackSize, identName });
            m_stackSize++;
        }

        if (regOrValue.starts_with("xmm")) {
            m_codeSectionAsmOutput << "    sub rsp, 8\n";
            m_codeSectionAsmOutput << "    movq [rsp], " << regOrValue << "\n";
            m_werePushesDouble.push_back(true);
        } else {
            m_codeSectionAsmOutput << "    push " << regOrValue << "\n";
            m_werePushesDouble.push_back(type == TokenType::dbl_lit);
        }
    }

    void pushIfStmtBoolVar(const std::string& regOrValue, const std::string& label, const bool& shouldIncreaseStackSize = true) {
        pushVar(
            regOrValue,
            "<VAR_BOOL_" + label + ">",
            TokenType::bool_lit,
            shouldIncreaseStackSize
        );
    }

    void pushInternalIntVar(const std::string& regOrValue, const bool& shouldIncreaseStackSize = true) {
        pushVar(
            regOrValue,
            internalVarIdent("INT"),
            TokenType::int_lit,
            shouldIncreaseStackSize
        );
    }

    void pushInternalBoolVar(const std::string& regOrValue, const bool& shouldIncreaseStackSize = true) {
        pushVar(
            regOrValue,
            internalVarIdent("BOOL"),
            TokenType::bool_lit,
            shouldIncreaseStackSize
        );
    }

    void pushInternalDblVar(const std::string& regOrValue, const bool& shouldIncreaseStackSize = true) {
        pushVar(
            regOrValue,
            internalVarIdent("DBL"),
            TokenType::dbl_lit,
            shouldIncreaseStackSize
        );
    }

    void pushConst(
        const std::string& reg,
        const std::string& identName = "",
        const TokenType& type = {},
        const size_t& dataLoc = 0,
        const std::string& valueAtDataLoc = ""
    ) {
        m_werePushesDouble.push_back(false); // Pushing a double const to stack means pushing its memory address, so the pushed value itself is not a double.

        if (!m_consts.contains(identName)) {
            m_consts.insert({ identName, Const { .dataLoc = dataLoc, .stackLoc = m_stackSize, .valueAtDataLoc = valueAtDataLoc, .type = type, .valueLength = valueAtDataLoc.length() } });
        }

        m_constsOnStack.insert({ m_stackSize, identName });

        m_codeSectionAsmOutput << "    push " << reg << "\n";
        m_stackSize++;
    }

    // Returns <ident, isConst, token type>
    std::optional<std::tuple<std::string, bool, TokenType>> pop(const std::string& reg) {
        m_wasSecondLastPopDbl = m_wasLastPopDbl;
        m_wasLastPopDbl = false;

        if (reg.starts_with("xmm")) {
            m_codeSectionAsmOutput << "    movq " << reg << ", [rsp]\n";
            m_codeSectionAsmOutput << "    add rsp, 8\n";
            m_wasLastPopDbl = true;
        } else {
            m_codeSectionAsmOutput << "    pop " << reg << "\n";
        }

        m_stackSize--;

        m_werePushesDouble.pop_back();

        if (m_constsOnStack.contains(m_stackSize)) {
            std::string ident = m_constsOnStack.at(m_stackSize);
            m_constsOnStack.erase(m_stackSize);

            TokenType type = m_consts.at(ident).type;
            m_wasLastPopDbl = type == TokenType::dbl_lit;
            return std::make_tuple(ident, true, type);
        }

        if (m_varsOnStack.contains(m_stackSize)) {
            std::string ident = m_varsOnStack.at(m_stackSize);
            m_varsOnStack.erase(m_stackSize);

            TokenType type = m_vars.at(ident).type;
            m_wasLastPopDbl = type == TokenType::dbl_lit;
            return std::make_tuple(ident, false, type);
        }

        return {};
    }

    void popWithConstNoStrLit(const std::string& reg, const std::string& xmmReg = "xmm0") {
        if (const auto popInfo = pop(m_werePushesDouble.back() ? xmmReg : reg)) {
            if (std::get<1>(popInfo.value())) {
                TokenType type = std::get<2>(popInfo.value());
                if (type == TokenType::str_lit) {
                    failUnexpectedDataType("string", m_lineNumber);
                }

                if (type == TokenType::int_lit || type == TokenType::bool_lit) {
                    // Just load the value from memory
                    m_codeSectionAsmOutput << "    mov " << reg << ", [" << reg << "]\n";
                }

                if (type == TokenType::dbl_lit) {
                    // Move value from memory into xmmReg register for floating point operations
                    m_codeSectionAsmOutput << "    xorps " << xmmReg << ", " << xmmReg << "\n";
                    m_codeSectionAsmOutput << "    movups " << xmmReg << ", [" << reg << "]\n";
                }
            }
        }
    }

    void popWithConstOnlyInt(const std::string& reg) {
        if (const auto popInfo = pop(reg)) {
            if (std::get<2>(popInfo.value()) != TokenType::int_lit) {
                failExpectedDifferentDataType("int", m_lineNumber);
            }
        }
    }

    std::string internalVarIdent(const std::string& additional = "") {
        m_internalVarsCount++;
        return "<VAR_" + additional + std::to_string(m_internalVarsCount) + ">";
    }

    std::string internalConstIdent(size_t dataLoc, std::string additional = "") {
        return "<CONST_" + additional + std::to_string(dataLoc) + ">";
    }

    size_t addDoubleToDataSection(double dbl) {
        m_dataSectionAsmOutput << "    dbl" << m_dataSize << " dq "
            << std::setprecision(15) << std::fixed << dbl << "\n";
        m_dataSize++;

        return m_dataSize - 1;
    }

    void beginScope() {
        m_scopes.push_back(std::make_tuple(m_vars.size(), m_varsOnStack.size(), m_stackSize));
    }

    void endScope() {
        auto scopeBeginInfo = m_scopes.back();
        size_t scopeVarCount = m_vars.size() - std::get<0>(scopeBeginInfo);
        size_t scopeVarsOnStackCount = m_varsOnStack.size() - std::get<1>(scopeBeginInfo);
        size_t scopeStackSize = m_stackSize - std::get<2>(scopeBeginInfo);

        for (int i = 0; i < scopeVarCount; i++) {
            m_vars.erase(m_varsInOrder.back());
            m_varsInOrder.pop_back();
        }

        auto varsOnStackIt = m_varsOnStack.end();
        for (int i = 0; i < scopeVarsOnStackCount; i++) {
            --varsOnStackIt;
        }
        m_varsOnStack.erase(varsOnStackIt, m_varsOnStack.end());

        m_codeSectionAsmOutput << "    add rsp, " << scopeStackSize * 8 << "\n";
        m_stackSize -= scopeStackSize;

        m_scopes.pop_back();
    }

    std::string getLabelName() {
        return ".label" + std::to_string(m_labelCount++);
    }

    void assignment(std::string ident, ExprNode* expr, std::string reassignedIdentifier = "") {
        bool isReassignment = !reassignedIdentifier.empty();

        TokenType tokenType = {};

        bool isTermNode = std::holds_alternative<TermNode*>(expr->var);
        bool isUnExprNode = std::holds_alternative<UnExprNode*>(expr->var);
        bool isBinExprNode = std::holds_alternative<BinExprNode*>(expr->var);

        if (!(isTermNode || isUnExprNode || isBinExprNode)) {
            failInvalidStmt(m_lineNumber);
        }

        if (isBinExprNode) {
            const size_t identStackLoc = m_stackSize;

            generateExpr(expr);

            if (m_werePushesDouble.back()) {
                tokenType = TokenType::dbl_lit;
            } else {
                tokenType = TokenType::int_lit;
            }

            // todo: this can set doubles as vars?
            m_vars.insert_or_assign(ident, Var { .stackLoc = identStackLoc, .type = tokenType });
            addToOrderedVarsIfNew(ident);

            return;
        }

        TermNode* term;
        std::vector<Token> unaryOperators = {};

        if (std::holds_alternative<TermNode*>(expr->var)) {
            term = std::get<TermNode*>(expr->var);

            // If the expr holds a ParenTerm (e.g. "let x = (1 + 2)"), we need to extract the expression from
            // within the parenthesis and use that instead. The parenthesis do not have any real purpose in such
            // a situation so it's not a problem to simply ignore them.
            // As the parser is already replacing `-(...)` with `-1 * (...)` we don't have to care about unary expression.
            if (std::holds_alternative<ParenTermNode*>(term->var)) {
                ParenTermNode* parenTerm = std::get<ParenTermNode*>(term->var);
                assignment(ident, parenTerm->expr, reassignedIdentifier);
                return;
            }
        } else {
            auto unExpr = std::get<UnExprNode*>(expr->var);
            term = unExpr->term;
            unaryOperators = unExpr->operators;
        }

        TokenType producedTokenType = {};

        // If the term is referencing another identifier, we need to copy what the other identifier is pointing to
        // and then let the new identifier point to that copy.
        if (std::holds_alternative<IdentTermNode*>(term->var)) {
            auto referencedIdentTerm = std::get<IdentTermNode*>(term->var);
            auto referencedIdent = referencedIdentTerm->ident.value.value();
            bool isReferencedConst = m_consts.contains(referencedIdent);

            if (!isReferencedConst && !m_vars.contains(referencedIdent)) {
                failUndeclaredIdentifer(referencedIdent, m_lineNumber);
            }

            if (isReferencedConst) {
                const auto& referencedConst = m_consts.at(referencedIdent);

                if (referencedConst.type == TokenType::str_lit && unaryOperators.size() > 0) {
                    failUexpectedUnaryOperator(unOpSymbol(unaryOperators.back().type), m_lineNumber);
                }

                size_t dataLoc = -1;
                std::string referencedTypeDataIdent = referencedConst.type == TokenType::dbl_lit ? "dbl" : "string";
                std::string valueAtDataLoc = "";

                if (referencedConst.type == TokenType::dbl_lit && negUnOperatorCondensed(unaryOperators) == "-") {
                    // Store negated value of the double.
                    valueAtDataLoc = "-" + m_consts.at(referencedIdent).valueAtDataLoc;
                    dataLoc = addDoubleToDataSection(std::stod(valueAtDataLoc));

                    m_codeSectionAsmOutput << "    mov rax, dbl" << dataLoc << "\n";
                    pushConst(
                        "rax",
                        internalConstIdent(dataLoc, "DBL"),
                        TokenType::dbl_lit,
                        dataLoc,
                        valueAtDataLoc
                    );
                } else {
                    dataLoc = referencedConst.dataLoc;
                    valueAtDataLoc = referencedConst.valueAtDataLoc;

                    m_codeSectionAsmOutput << "    mov rax, " << referencedTypeDataIdent << dataLoc << "\n";
                    pushConst("rax", ident, referencedConst.type, dataLoc, valueAtDataLoc);
                }

                if (isReassignment) {
                    Const* reassignedCons = &m_consts.at(ident);
                    reassignedCons->dataLoc = dataLoc;
                    reassignedCons->valueAtDataLoc = valueAtDataLoc;
                    reassignedCons->valueLength = valueAtDataLoc.length();
                }

                producedTokenType = referencedConst.type;
            } else {
                const auto& referencedVar = m_vars.at(referencedIdent);

                bool hasUnPlusMinus = false;
                for (Token unOperator : unaryOperators) {
                    if (unOperator.type == TokenType::plus || unOperator.type == TokenType::minus) {
                        hasUnPlusMinus = true;
                        break;
                    }
                }

                bool isNegated = negUnOperatorCondensed(unaryOperators) == "-";

                std::stringstream stackLocation;
                stackLocation << "QWORD [rsp + " << (m_stackSize - referencedVar.stackLoc - 1) * 8 << "]";

                if (isNegated) {
                    if (referencedVar.type == TokenType::int_lit || referencedVar.type == TokenType::bool_lit) {
                        m_codeSectionAsmOutput << "    mov rax, " << stackLocation.str() << "\n";
                        m_codeSectionAsmOutput << "    neg rax" << "\n";
                        pushInternalIntVar("rax");
                    } else if (referencedVar.type == TokenType::dbl_lit) {
                        m_codeSectionAsmOutput << "    movq xmm0, " << stackLocation.str() << "\n";
                        m_codeSectionAsmOutput << "    movq xmm1, [FLIP_S_64B]\n";
                        m_codeSectionAsmOutput << "    xorps xmm0, xmm1\n";
                        pushInternalDblVar("xmm0");
                    } else {
                        failUexpectedUnaryOperator(unOpSymbol(unaryOperators.back().type), m_lineNumber);
                    }
                } else if (hasUnPlusMinus && referencedVar.type == TokenType::bool_lit) {
                    m_codeSectionAsmOutput << "    mov rax, " << stackLocation.str() << "\n";
                    pushInternalIntVar("rax");
                } else {
                    m_codeSectionAsmOutput << "    mov rax, " << stackLocation.str() << "\n";
                    pushVar("rax", ident, referencedVar.type);
                }

                producedTokenType = referencedVar.type;
            }
        } else { // term does not reference another identifier
            size_t valueLength = 0;
            std::string constValue = "";
            if (std::holds_alternative<StrLitTermNode*>(term->var)) {
                if (unaryOperators.size() > 0) {
                    failUexpectedUnaryOperator(unOpSymbol(unaryOperators.back().type), m_lineNumber);
                }

                tokenType = TokenType::str_lit;
                constValue = std::get<StrLitTermNode*>(term->var)->str_lit.value.value();
                valueLength = constValue.length();
            } else if (std::holds_alternative<BoolLitTermNode*>(term->var)) {
                tokenType = TokenType::bool_lit;
            } else if (std::holds_alternative<IntLitTermNode*>(term->var)) {
                tokenType = TokenType::int_lit;
            } else if (std::holds_alternative<DblLitTermNode*>(term->var)) {
                tokenType = TokenType::dbl_lit;
                std::string negUnCondensed = negUnOperatorCondensed(unaryOperators);
                constValue = negUnCondensed + std::get<DblLitTermNode*>(term->var)->dbl_lit.value.value();
                valueLength = constValue.length();
            }

            if (tokenType == TokenType::str_lit || tokenType == TokenType::dbl_lit) {
                if (isReassignment) {
                    Const* reassignedCons = &m_consts.at(ident);
                    reassignedCons->dataLoc = m_dataSize;
                    reassignedCons->valueAtDataLoc = constValue;
                    reassignedCons->valueLength = valueLength;
                } else {
                    m_consts.insert({ ident, Const { .dataLoc = m_dataSize, .stackLoc = m_stackSize, .valueAtDataLoc = constValue, .type = tokenType, .valueLength = valueLength } });
                }
            } else if (!isReassignment) {
                // Vars only need to be updated if this is a new assignment, because they only hold stack location and
                // token type, which don't change in a reassignment.
                m_vars.insert({ ident, Var { .stackLoc = m_stackSize, .type = tokenType } });
                addToOrderedVarsIfNew(ident);
            }

            producedTokenType = generateExpr(expr,{}, isReassignment);
        }

        if (!isReassignment) {
            return;
        }

        TokenType reassignedTokenType = {};
        if (m_consts.contains(ident)) {
            reassignedTokenType = m_consts.at(ident).type;
        }
        if (m_vars.contains(ident)) {
            reassignedTokenType = m_vars.at(ident).type;
        }

        if (reassignedTokenType != producedTokenType) {
            failInvalidReassignment(tokenTypeName(reassignedTokenType), m_lineNumber);
        }

        if (reassignedTokenType == TokenType::int_lit || reassignedTokenType == TokenType::bool_lit) {
            m_codeSectionAsmOutput << "    add rsp, " << (m_stackSize - m_vars.at(ident).stackLoc - 1) * 8 << "\n";
            m_codeSectionAsmOutput << "    mov [rsp], rax\n";
            m_codeSectionAsmOutput << "    sub rsp, " << (m_stackSize - m_vars.at(ident).stackLoc - 1) * 8 << "\n";
        }

        if (reassignedTokenType == TokenType::dbl_lit || reassignedTokenType == TokenType::str_lit) {
            m_codeSectionAsmOutput << "    add rsp, " << (m_stackSize - m_consts.at(ident).stackLoc - 1) * 8 << "\n";
            m_codeSectionAsmOutput << "    mov [rsp], rax\n";
            m_codeSectionAsmOutput << "    sub rsp, " << (m_stackSize - m_consts.at(ident).stackLoc - 1) * 8 << "\n";
        }
    }

    void addToOrderedVarsIfNew(std::string ident) {
        // Only add to m_varsInOrder if not already part of it.
        if (std::find(m_varsInOrder.begin(), m_varsInOrder.end(), ident) == std::end(m_varsInOrder)) {
            m_varsInOrder.push_back(ident);
        }
    }

    /*
    * Input:
    *     edi: integer to convert
    * Output:
    *     rsi (points to first char)
    *     rdx (contains number of chars)
    * Clobbers rsi, rax, rcx, rdx
    */
    void addIntToAsciiAsm() {
        // reference: https://stackoverflow.com/a/46301894
        m_codeSectionAsmOutput << "itoa:\n";

        m_codeSectionAsmOutput << "    mov rax, rdi\n"; // Expects int to convert to be in edi
        m_codeSectionAsmOutput << "    mov ecx, 0xa\n"; // Store 10 as base
        m_codeSectionAsmOutput << "    mov rsi, rsp\n"; // Stack pointer to rsi

        // Negate value if it is negative to make sure we print the correct number
        m_codeSectionAsmOutput << "    test rax, rax\n";
        m_codeSectionAsmOutput << "    jns .itoaNotNegative\n";
        m_codeSectionAsmOutput << "    neg rax\n";
        m_codeSectionAsmOutput << "    .itoaNotNegative:\n";

        m_codeSectionAsmOutput << "\n";
        m_codeSectionAsmOutput << "    .itoaDigitToAsciiLoop:\n";
        m_codeSectionAsmOutput << "        xor edx, edx\n";
        m_codeSectionAsmOutput << "        div rcx\n"; // divide eax by 10, remainder in edx
        m_codeSectionAsmOutput << "        add edx, '0'\n"; // Convert remainder to ascii (adding ascii value of 0)
        m_codeSectionAsmOutput << "        dec rsi\n"; // move stack pointer down
        m_codeSectionAsmOutput << "        mov [rsi], dl\n"; // add lowest byte from edx (ascii value of remainder) to stack
        m_codeSectionAsmOutput << "        test rax, rax\n"; // ZeroFlag set to 1 if eax is 0
        m_codeSectionAsmOutput << "        jnz .itoaDigitToAsciiLoop\n"; // process next digit if still number (non-zero) in eax
        m_codeSectionAsmOutput << "\n";

        // Add "-" char if needed
        m_codeSectionAsmOutput << "    test rdi, rdi\n"; // Test to get SF (sign flag) (rdi still holds original value)
        m_codeSectionAsmOutput << "    jns .itoaNoNegativeSign\n"; // jmp if no sign
        m_codeSectionAsmOutput << "    dec rsi\n"; // move stack pointer down
        m_codeSectionAsmOutput << "    mov BYTE [rsi], '-'\n";
        m_codeSectionAsmOutput << "    .itoaNoNegativeSign:\n";

        m_codeSectionAsmOutput << "    lea edx, [rsp]\n"; // address of rsp to edx
        m_codeSectionAsmOutput << "    sub edx, esi\n"; // subtract address in esi from edx to get length between last and first char
        m_codeSectionAsmOutput << "    ret\n";
        m_codeSectionAsmOutput << "\n";

        // rsi now points to first digit/negative sign (in ascii) in memory, other digits are stored above in memory
        // rdx now contains the number of chars that were stored in memory
    }

    /*
    * Input:
    *     rdi: memory address of double to be converted or
    *     xmm0: double to be converted (used if rdi is 0)
    * Output:
    *     rsi (points to first char)
    *     rdx (contains number of chars)
    * Clobbers rsi, rax, rbx, rcx, rdx, r8, xmm0, xmm1, xmm2
    */
    void addDoubleToAsciiAsm() {
        // reference: https://stackoverflow.com/a/46301894
        m_codeSectionAsmOutput << "dtoa:\n";

        // Extract only whole number part into xmm1
        m_codeSectionAsmOutput << "    test rdi, rdi\n"; // Expects double to already be in xmm0 if rdi is 0
        m_codeSectionAsmOutput << "    jz .dtoaDoubleAlreadyLoaded\n"; // Expects double to already be in xmm0 if rdi is 0
        m_codeSectionAsmOutput << "    movups xmm0, [rdi]\n"; // Expects address of double to convert to be in rdi
        m_codeSectionAsmOutput << "    .dtoaDoubleAlreadyLoaded:\n";

        // Negate value if it is negative to make sure we print the correct number
        m_codeSectionAsmOutput << "    xor r8, r8\n";
        m_codeSectionAsmOutput << "    movmskpd r8, xmm0\n"; // Extract sign bits into r8
        m_codeSectionAsmOutput << "    and r8, 0x1\n"; // We only care for the lowest bit
        m_codeSectionAsmOutput << "    test r8, r8\n"; // If r8 is not 0 then number in xmm0 is negative
        m_codeSectionAsmOutput << "    jz .dtoaNotNegative\n";
        m_codeSectionAsmOutput << "    movq xmm1, [FLIP_S_64B]\n";
        m_codeSectionAsmOutput << "    xorps xmm0, xmm1\n";
        m_codeSectionAsmOutput << "    .dtoaNotNegative:\n";

        m_codeSectionAsmOutput << "    cvttsd2si rdi, xmm0\n"; // Whole number (before decimal point) to int in rdi

        // Extract fractional part and add 0.1 to keep leading 0 when converting to int
        m_codeSectionAsmOutput << "    cvtsi2sd xmm1, rdi\n"; // Whole number to xmm1
        m_codeSectionAsmOutput << "    subsd xmm0, xmm1\n"; // Sub whole number from double (leaving fractional part)
        m_codeSectionAsmOutput << "    mov rax, DBL_0_1\n"; // Let rax point to 0.1
        m_codeSectionAsmOutput << "    movups xmm1, [rax]\n"; // Load 0.1 to xmm1
        m_codeSectionAsmOutput << "    addsd xmm0, xmm1\n"; // Add 0.1 to xmm0 (important for e.g. 0.002 -> 0.102 to keep leading zeros later)

        // Initialize bl to determine if fractional part started with 9. Then the number now is equal to/bigger than one, which needs to be taken into account later.
        m_codeSectionAsmOutput << "    xor bl, bl\n"; // Clean bl
        m_codeSectionAsmOutput << "    mov rcx, DBL_1_0\n";
        m_codeSectionAsmOutput << "    movups xmm2, [rcx]\n";
        m_codeSectionAsmOutput << "    comisd xmm0, xmm2\n"; // Compare xmm0 to 1.0
        m_codeSectionAsmOutput << "    jb .dtoaFractionDidNotStartWith9\n"; // Compare xmm0 to 1.0
        m_codeSectionAsmOutput << "    mov bl, 1\n"; // 1 to bl to indicate that fractional part started with 9
        m_codeSectionAsmOutput << "    .dtoaFractionDidNotStartWith9:\n"; // 1 to bh

        // Convert fractional part to int
        m_codeSectionAsmOutput << "    mov rax, MUL_FRAC_TO_INT\n"; // Let rax point to big number for multiplication of fractional part
        m_codeSectionAsmOutput << "    movups xmm1, [rax]\n"; // Load MUL_FRAC_TO_INT to xmm1
        m_codeSectionAsmOutput << "    mulsd xmm0, xmm1\n"; // Multiply fracitonal part to make it an int
        m_codeSectionAsmOutput << "    cvttsd2si rax, xmm0\n"; // Move fractional part as int to rax
        // -> rdi holds part before decimal point, rax holds part after decimal point

        m_codeSectionAsmOutput << "    mov ecx, 0xa\n"; // Store 10 as base
        m_codeSectionAsmOutput << "    mov rsi, rsp\n"; // Stack pointer to rsi

        // Remove trailing zeros from rax (from decimal places)
        m_codeSectionAsmOutput << "\n";
        m_codeSectionAsmOutput << "    .dtoaRemoveTrailingZerosLoop:\n";
        m_codeSectionAsmOutput << "        xor edx, edx\n"; // Empty edx
        m_codeSectionAsmOutput << "        div rcx\n"; // Divide eax by 10, remainder in edx
        m_codeSectionAsmOutput << "        test edx, edx\n"; // If remainder is zero, we have a trailing zero
        m_codeSectionAsmOutput << "        jz .dtoaRemoveTrailingZerosLoop\n";
        m_codeSectionAsmOutput << "\n";

        // Convert first non-0 digit of rax to ascii (as this is the remainder left in edx from previous loop)
        m_codeSectionAsmOutput << "    add edx, '0'\n"; // Convert remainder to ascii (adding ascii value of 0)
        m_codeSectionAsmOutput << "    dec rsi\n"; // Move stack pointer down
        m_codeSectionAsmOutput << "    mov [rsi], dl\n"; // Add lowest byte from edx (ascii value of remainder) to stack

        // Initialize bh to determine if number before or after decimal point is converted (1 == after, 0 == before)
        m_codeSectionAsmOutput << "    xor bh, bh\n"; // Clean bh
        m_codeSectionAsmOutput << "    mov bh, 1\n"; // 1 to bh

        // If rax is 0 then there was only one number in fractional part
        m_codeSectionAsmOutput << "    test rax, rax\n"; // ZeroFlag set to 1 if eax is 0
        m_codeSectionAsmOutput << "    jz .dtoaAfterFracConv\n";

        // Transform rax to char array
        m_codeSectionAsmOutput << "\n";
        m_codeSectionAsmOutput << "    .dtoaDigitToAsciiLoop:\n";
        m_codeSectionAsmOutput << "        xor edx, edx\n"; // Empty edx
        m_codeSectionAsmOutput << "        div rcx\n"; // Divide eax by 10, remainder in edx
        m_codeSectionAsmOutput << "        add edx, '0'\n"; // Convert remainder to ascii (adding ascii value of 0)
        m_codeSectionAsmOutput << "        dec rsi\n"; // Move stack pointer down
        m_codeSectionAsmOutput << "        mov [rsi], dl\n"; // Add lowest byte from edx (ascii value of remainder) to stack
        m_codeSectionAsmOutput << "        test rax, rax\n"; // ZeroFlag set to 1 if eax is 0
        m_codeSectionAsmOutput << "        jnz .dtoaDigitToAsciiLoop\n"; // Process next digit if still number (non-zero) in eax
        m_codeSectionAsmOutput << "\n";

        m_codeSectionAsmOutput << "    .dtoaAfterFracConv:\n"; // Jump here to not do conversion of fractional part (needed if only 1 number inf ractional part)
        m_codeSectionAsmOutput << "    test bh, bh\n"; // If 0, all numbers are converted
        m_codeSectionAsmOutput << "    jz .dtoa_finish\n";

        // Revert the adding of 0.1 above (rsi currently points to char of first digit after decimal point)
        m_codeSectionAsmOutput << "    sub edx, '0'\n"; // Convert remainder back to int
        m_codeSectionAsmOutput << "    test bl, bl\n"; // If bl is zero, 1 needs to be subtracted from value, because it wasn't 9 before
        m_codeSectionAsmOutput << "    jnz .dtoaFirstCharInReaminderNeedsToBe9\n"; // Else the change value to 9
        m_codeSectionAsmOutput << "    sub edx, 1\n"; // Subtract one
        m_codeSectionAsmOutput << "    add edx, '0'\n"; // Ascii again
        m_codeSectionAsmOutput << "    jmp .dtoaOverrideFirstCharInRemainder\n"; // Jump next part
        m_codeSectionAsmOutput << "    .dtoaFirstCharInReaminderNeedsToBe9:\n"; // Subtract one
        m_codeSectionAsmOutput << "    mov edx, '9'\n"; // Set edx to char '9'
        m_codeSectionAsmOutput << "    .dtoaOverrideFirstCharInRemainder:\n"; // Subtract one
        m_codeSectionAsmOutput << "    mov [rsi], dl\n"; // Override previous char

        // Add decimal point char
        m_codeSectionAsmOutput << "    dec rsi\n"; // Move stack pointer down
        m_codeSectionAsmOutput << "    mov BYTE [rsi], '.'\n"; // Add ascii value of '.' to stack

        // Convert number before decimal point to char array
        m_codeSectionAsmOutput << "    mov rax, rdi\n"; // Move number to rax
        m_codeSectionAsmOutput << "    mov bh, 0\n"; // 0 to bh (-> meaning that now number before decimal point is converted)
        m_codeSectionAsmOutput << "    jmp .dtoaDigitToAsciiLoop\n";

        m_codeSectionAsmOutput << "\n";
        m_codeSectionAsmOutput << "    .dtoa_finish:\n";

        // Add "-" char if needed
        m_codeSectionAsmOutput << "        test r8, r8\n"; // If r8 is not 0 then number in xmm0 is negative
        m_codeSectionAsmOutput << "        jz .dtoaNoNegativeSign\n";
        m_codeSectionAsmOutput << "        dec rsi\n";
        m_codeSectionAsmOutput << "        mov BYTE [rsi], '-'\n";
        m_codeSectionAsmOutput << "        .dtoaNoNegativeSign:\n";

        m_codeSectionAsmOutput << "        lea edx, [rsp]\n"; // Address of rsp to edx
        m_codeSectionAsmOutput << "        sub edx, esi\n"; // Subtract address in esi from edx to get length between last and first char
        m_codeSectionAsmOutput << "        ret\n";
        m_codeSectionAsmOutput << "\n";

        // rsi now points to first digit/negative sign (in ascii) in memory, other digits are stored above in memory
        // rdx now contains the number of chars that were stored in memory
    }

    // TODO: Currently unsued, keep for later :D
    void addCharacterCountAsm() {
        m_codeSectionAsmOutput << "charCount:\n";

        m_codeSectionAsmOutput << "    .charCountLoop:\n";
        m_codeSectionAsmOutput << "        mov rsi, rdi\n"; // Expects chars to count to be in rdi
        m_codeSectionAsmOutput << "        xor rdx, rdx\n"; // Set to 0, used as char counter
        m_codeSectionAsmOutput << "        cmp byte [rsi], 0x00\n"; // Expecting null terminating string
        m_codeSectionAsmOutput << "        je .charCountDone\n";
        m_codeSectionAsmOutput << "        inc rdx\n";
        m_codeSectionAsmOutput << "        inc rsi\n";
        m_codeSectionAsmOutput << "        jmp .charCountLoop\n";

        m_codeSectionAsmOutput << "    .charCountDone:\n";
        m_codeSectionAsmOutput << "        ret\n";
        m_codeSectionAsmOutput << "\n";
    }

    /*
    * Input:
    *     rdi -> an int that can be converted to bool [0 will be interpreted as false, everything else as true]
    * Output:
    *     rsi -> memory address of begin of correct string for given bool ("false" or "true")
    *     rdx -> length of the string (5 for "false", 4 for "true")
    * Clobbers rsi, rdx
    */
    void addMovBoolStrAsm() {
        m_codeSectionAsmOutput << "movBoolStr:\n";

        m_codeSectionAsmOutput << "    test rdi, rdi\n"; // If 0, it's "false"
        m_codeSectionAsmOutput << "    jnz .mov_bool_str_not_false\n";
        m_codeSectionAsmOutput << "        mov rsi, STRING_FALSE\n";
        m_codeSectionAsmOutput << "        mov rdx, 5\n";
        m_codeSectionAsmOutput << "        ret\n";
        m_codeSectionAsmOutput << "    .mov_bool_str_not_false:\n";
        m_codeSectionAsmOutput << "        mov rsi, STRING_TRUE\n";
        m_codeSectionAsmOutput << "        mov rdx, 4\n";
        m_codeSectionAsmOutput << "        ret\n";
        m_codeSectionAsmOutput << "\n";
    }

    /*
    * Input as defined for sys_write sycall:
    *     rdi -> output file descriptor [1 - stdout, 2 - stderr]
    *     rsi -> pointer to first char
    *     rdx -> length of string
    * Clobbers rax
    */
    void addSysWriteAsm() {
        m_codeSectionAsmOutput << "sysWrite:\n";

        m_codeSectionAsmOutput << "    mov eax, 1\n"; // 1 is sys_write syscall
        m_codeSectionAsmOutput << "    syscall\n";
        m_codeSectionAsmOutput << "    ret\n";
        m_codeSectionAsmOutput << "\n";
    }

    struct Var {
        size_t stackLoc;
        TokenType type;
    };

    struct Const {
        size_t dataLoc;
        size_t stackLoc;
        std::string valueAtDataLoc;
        TokenType type;
        size_t valueLength = 0;
    };

    const ProgNode m_prog;
    size_t m_lineNumber = 1;
    std::stringstream m_asmOutput;
    std::stringstream m_dataSectionAsmOutput;
    std::stringstream m_codeSectionAsmOutput;
    bool m_containsCustomExitCall = false;
    size_t m_stackSize = 0;
    size_t m_dataSize = 0;
    size_t m_internalVarsCount = 0;
    size_t m_labelCount = 0;
    bool m_wasLastPopDbl = false;
    bool m_wasSecondLastPopDbl = false;
    std::vector<bool> m_werePushesDouble {};
    /** var ident => var */
    std::unordered_map<std::string, Var> m_vars {};
    /** stack loc => var ident */
    std::map<size_t, std::string> m_varsOnStack {};
    /** var ident ordered by insertion time (first was added first) */
    std::vector<std::string> m_varsInOrder {};
    /** const ident => const */
    std::unordered_map<std::string, Const> m_consts {};
    /** stack loc => const ident */
    std::unordered_map<size_t, std::string> m_constsOnStack = {};
    /** values at scope start: <vars count, varsOnStack count, stackSize> */
    std::vector<std::tuple<size_t, size_t, size_t>> m_scopes = {};
};
