#include "parser.h"

namespace TeX {
Parser::Parser(const std::string& filename) : LexerBase(filename) {
    NextChar();
    NextToken();
}

void Parser::LexLineComment() {
    /// Lexer is at '%'
    token->string_content = ReadUntilChar('\n');

    /// Append the newline and discard it
    if (!at_eof) {
        token->string_content += U'\n';
        NextChar();
    }
}

bool IsLetter(Char c) {
    return (U'a' <= c && c <= U'z') || (U'A' <= c && c <= U'Z') || c == U'@';
}

void Parser::LexCommandSequence() {
    /// Lexer is at '\'
    NextChar();

    if (at_eof) LEXER_ERROR("Dangling backslash at end of file");

    token->string_content = U"\\";

    /// Check if the command sequence is one of \&, \#, ...
    /// If so, append that character and return
    if (!IsLetter(lastc)) {
        token->string_content += lastc;
        NextChar();
        return;
    }

    /// Otherwise, keep reading till we find something that is
    /// not a letter
    do {
        token->string_content += lastc;
        NextChar();
    } while ((IsLetter(lastc)));
}

void Parser::LexText() {
    static const std::vector<Char> special_characters = {U'%', U'\\', U'{', U'}'};
    token->string_content                             = ReadUntilChar(special_characters);
}

void Parser::NextToken() {
    token      = new Token();
    token->loc = Here();

    if (at_eof) {
        token->type = TokenType::EndOfFile;
        return;
    }

    token->type = TokenType(lastc);
    switch (lastc) {
        case U'%': return LexLineComment();
        case U'\\': return LexCommandSequence();
        case U'{':
        case U'}':
            NextChar();
            return;
        default:
            token->type = TokenType::Text;
            LexText();
    }
}

String StringiseType(const Parser::Token* token) {
    using enum TokenType;
    String s;
    s += (String) token->loc;
    s += U": ";
    switch (token->type) {
        case Invalid: s += U"[Invalid: "; break;
        case Text: s += U"[Text: "; break;
        case EndOfFile: s += U"[EndOfFile]\n"; return s;
        case CommandSequence: s += U"[CommandSequence: "; break;
        case LineComment: s += U"[LineComment: "; break;
        case GroupBegin: s += U"[GroupBegin]\n"; return s;
        case GroupEnd: s += U"[GroupEnd]\n"; return s;
    }
    s += Escape(token->string_content) + U"]\n";
    return s;
}

} // namespace TeX
