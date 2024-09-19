#pragma once

#include <iostream>
#include <optional>
#include <vector>

#include "./utils.hpp"

const std::vector<std::string> BUILT_IN_FUNC_NAMES {"print", "println", "exit"};

class Tokenizer {
public:
    Tokenizer(const std::string source) : m_source(std::move(source)) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        std::string buf;

        while (peek().has_value()) {
            if (std::isalpha(peek().value())) {
                buf.push_back(consume());

                while (peek().has_value() && std::isalnum(peek().value())) {
                    buf.push_back(consume());
                }

                if (buf == "let") {
                    tokens.push_back({ .type = TokenType::let });
                } else if (buf == "if") {
                    tokens.push_back({ .type = TokenType::if_ });
                } else if (buf == "else") {
                    if (
                        peek().has_value() && peek().value() == ' '
                        && peek(1).has_value() && peek(1).value() == 'i'
                        && peek(2).has_value() && peek(2).value() == 'f'
                    ) {
                        consume();
                        consume();
                        consume();
                        tokens.push_back({ .type = TokenType::elseif });
                    } else {
                        tokens.push_back({ .type = TokenType::else_ });
                    }
                } else if (buf == "elseif") {
                    tokens.push_back({ .type = TokenType::elseif });
                } else if (buf == "true" || buf == "false") {
                    tokens.push_back({ .type = TokenType::bool_lit, .value = buf });
                } else if (auto builtInFuncToken = evaluateBuiltInFunc(buf)) {
                    tokens.push_back(builtInFuncToken.value());
                } else {
                    tokens.push_back({ .type = TokenType::ident, .value = buf });
                }

                buf.clear();
            } else if (std::isdigit(peek().value())) {
                buf.push_back(consume());

                while (peek().has_value() && std::isdigit(peek().value())) {
                    buf.push_back(consume());
                }

                if (!peek().has_value() || peek().value() != '.') {
                    tokens.push_back({ .type = TokenType::int_lit, .value = buf });
                    buf.clear();
                    continue;
                }

                // The "."
                buf.push_back(consume());

                if (!peek().has_value() || !std::isdigit(peek().value())) {
                    failUnxpectedNonDigit(peek().has_value() ? peek().value() : ' ', m_lineNumber);
                }

                while (peek().has_value() && std::isdigit(peek().value())) {
                    buf.push_back(consume());
                }

                tokens.push_back({ .type = TokenType::dbl_lit, .value = buf });
                buf.clear();
            } else if (peek().value() == '"') {
                tokens.push_back({.type = TokenType::quot});
                consume();
                // Consume until next quotation mark, because it's a string.
                while (peek().has_value()) {
                    if (peek().value() == '\\') {
                        int slashCount = 0;
                        while (peek().has_value() && peek().value() == '\\') {
                            slashCount++;
                            if (slashCount % 2 == 0) {
                                buf.push_back(consume());
                            } else {
                                consume();
                            }
                        }

                        if (slashCount % 2 != 0) {
                            if (peek().value() == '"') {
                                buf.push_back(consume());
                                continue;
                            } else {
                                failUnxpectedEscape(peek().value(), m_lineNumber);
                            }
                        }
                    }

                    if (peek().value() == '"') {
                        break;
                    }

                    buf.push_back(consume());
                }

                tokens.push_back({.type = TokenType::str_lit, .value = buf});
                buf.clear();

                // Consume ending quotation mark.
                if (peek().has_value()) {
                    tokens.push_back({.type = TokenType::quot});
                    consume();
                }
            } else if (peek().value() == '=') {
                tokens.push_back({.type = TokenType::eq});
                consume();
            } else if (peek().value() == '(') {
                consume();
                tokens.push_back({ .type = TokenType::open_paren });
            } else if (peek().value() == ')') {
                consume();
                tokens.push_back({ .type = TokenType::close_paren });
            } else if (peek().value() == '{') {
                consume();
                tokens.push_back({ .type = TokenType::open_curly });
            } else if (peek().value() == '}') {
                consume();
                tokens.push_back({ .type = TokenType::close_curly });
            } else if (peek().value() == ';') {
                tokens.push_back({ .type = TokenType::semi });
                consume();
            } else if (peek().value() == '+') {
                tokens.push_back({ .type = TokenType::plus });
                consume();
            } else if (peek().value() == '-') {
                tokens.push_back({ .type = TokenType::minus });
                consume();
            } else if (peek().value() == '*') {
                tokens.push_back({ .type = TokenType::star });
                consume();
            } else if (peek().value() == '/') {
                if (peek(1).value() == '/') {
                    // Discard until new line, because it's a comment
                    while (peek().has_value() && peek().value() != '\n') {
                        consume();
                    }
                } else if (peek(1).value() == '*') {
                    // Discard until */ because it's a multiline comment
                    while (peek().has_value() && peek(1).has_value()) {
                        if (peek().value() == '*' && peek(1).value() == '/') {
                            consume();
                            consume();
                            break;
                        }

                        if (peek().value() == '\n') {
                            m_lineNumber++;
                            tokens.push_back({ .type = TokenType::line_break });
                        }

                        consume();
                    }
                } else {
                    tokens.push_back({ .type = TokenType::slash });
                    consume();
                }
            } else if (peek().value() == '\n') {
                m_lineNumber++;
                tokens.push_back({ .type = TokenType::line_break });
                consume();
            } else if (peek(1).has_value() && peek().value() == '\r' && peek(1).value() == '\n') {
                m_lineNumber++;
                tokens.push_back({ .type = TokenType::line_break });
                consume();
                consume();
            } else if (std::isspace(peek().value())) {
                consume();
            } else {
                failUnknownChar(peek().value(), m_lineNumber);
            }
        }

        m_index = 0;
        return tokens;
    }

private:
    [[nodiscard]] std::optional<char> peek(int offset = 0) const {
        if (m_index + offset >= 0 && m_index + offset >= m_source.length()) {
            return {};
        } else {
            return m_source.at(m_index + offset);
        }
    }

    char consume() {
        return m_source.at(m_index++);
    }

    std::optional<Token> evaluateBuiltInFunc(std::string funcName) {
        if (std::find(BUILT_IN_FUNC_NAMES.begin(), BUILT_IN_FUNC_NAMES.end(), funcName) == BUILT_IN_FUNC_NAMES.end()) {
            return {};
        }

        Token token;
        token.type = TokenType::built_in_func;
        token.value = funcName;
        return token;
    }

    const std::string m_source;
    size_t m_index = 0;
    size_t m_lineNumber = 1;
};
