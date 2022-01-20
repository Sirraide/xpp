#include "parser.h"

#include <fmt/format.h>

namespace TeX {
bool IsSpace(U32 c) {
    return c == U32(' ') || c == U32('\t') || c == U32('\n')
           || c == U32('\r') || c == U32('\v') || c == U32('\f');
}

template <typename TString>
void TrimInitial(TString& str) {
    U64 len = 0;
    while (len < str.size() && IsSpace(U32(str[len]))) len++;
    str.erase(0, len);
}

Parser::Parser(const Clopts& _opts) : LexerBase(_opts["filename"].AsString()), opts(_opts) {
    if (opts["-o"].Found()) output_file = fopen(opts["-o"].AsString().c_str(), "w");
    else output_file = stdout;
    if (!output_file) Die("Could not open output file: %s", strerror(errno));

    Parser::NextChar();
    Parser::NextToken();
    if (opts["--print-tokens"].Found()) {
        Parser::PrintAllTokens(output_file);
        exit(0);
    }
    Parser::Parse();
    if (!has_error) Parser::EmitNodes(tokens);
}

void Parser::LexLineComment() {
    /// Lexer is at '%'
    token.string_content = ReadUntilChar('\n');

    /// Append the newline and discard it
    if (!at_eof) {
        token.string_content += U'\n';
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

    token.string_content = U"\\";

    /// Check if the command sequence is one of \&, \#, ...
    /// If so, append that character and return
    if (!IsLetter(lastc)) {
        token.string_content += lastc;
        NextChar();
        return;
    }

    /// Otherwise, keep reading till we find something that is
    /// not a letter
    do {
        token.string_content += lastc;
        NextChar();
    } while ((IsLetter(lastc)));
}

void Parser::LexText() {
    static const std::vector<Char> special_characters = {U'%', U'\\', U'{', U'}'};
    token.string_content                              = ReadUntilChar(special_characters);
}

void Parser::NextToken() {
    token     = Token();
    token.loc = Here();

    if (at_eof) {
        token.type = TokenType::EndOfFile;
        return;
    }

    token.type = TokenType(lastc);
    switch (lastc) {
        case U'%': return LexLineComment();
        case U'\\': return LexCommandSequence();
        case U'{':
        case U'}':
            NextChar();
            return;
        default:
            token.type = TokenType::Text;
            LexText();
    }
}

void Parser::Parse() {
    do {
        ParseSequence();
        tokens.push_back(token);
        NextToken();
    } while (token.type != T::EndOfFile);
}

void Parser::ParseSequence() {
    using enum TokenType;
    switch (token.type) {
        case GroupBegin:
            group_count++;
            break;
        case GroupEnd:
            group_count--;
            break;
        case CommandSequence:
            ParseCommandSequence();
            break;
        default:;
    }
}

NodeList Parser::ParseGroup(bool in_macrodef) {
    Expect(TokenType::GroupBegin);
    group_count++;
    auto here = Here();
    NextToken(); /// yeet '{'

    if (at_eof) Error(here, "Group terminated by end of file");

    NodeList lst;
    U64      depth = group_count;
    while (!at_eof) {
        if (in_macrodef)
            for (;;) {
                if (token.type != TokenType::LineComment) break;
                do NextToken();
                while (token.type == TokenType::LineComment);
                if (token.type == TokenType::Text) {
                    TrimInitial(token.string_content);
                }
            }
        ParseSequence();
        if (token.type == TokenType::GroupEnd && group_count < depth) break;
        lst.push_back(token);
        NextToken();
    }

    if (at_eof) Error(here, "Group terminated by end of file");
    if (token.type != TokenType::GroupEnd) Unreachable("ParseGroup");
    group_count--;
    NextToken(); /// yeet '}'

    return lst;
}

void Parser::ParseCommandSequence() {
    if (token.string_content == U"\\XDefine") {
        NextToken(); /// yeet '\XDefine'
        Expect(TokenType::CommandSequence);
        String cs = token.string_content;

        NextToken(); /// yeet csname
        auto group = ParseGroup(true);
        macros[cs] = group;
    }
}

void Parser::Expect(TokenType type) {
    if (token.type != type) Error(Here(), "Expected token type %d, but was %d", int(type), int(token.type));
}

void Parser::EmitNodes(const NodeList& nodes) {
    using enum TokenType;
    for (const auto& node : nodes) {
        switch (node.type) {
            case GroupBegin:
                fmt::print(output_file, "{{");
                break;
            case GroupEnd:
                fmt::print(output_file, "}}");
                break;
            case CommandSequence:
                if (macros.contains(node.string_content)) EmitNodes(macros[node.string_content]);
                else fmt::print(output_file, "{}", ToUTF8(node.string_content));
                break;
            case Macro:
                Unreachable("EmitNodes: Macro should have been removed from NodeList");
            default:
                fmt::print(output_file, "{}", ToUTF8(node.string_content));
        }
    }
}

String StringiseType(const Node& token) {
    using enum TokenType;
    String s;
    s += (String) token.loc;
    s += U": ";
    switch (token.type) {
        case Invalid: s += U"[Invalid: "; break;
        case Text: s += U"[Text: "; break;
        case EndOfFile: s += U"[EndOfFile]\n"; return s;
        case CommandSequence: s += U"[CommandSequence: "; break;
        case Macro: s += U"[Macro: "; break;
        case LineComment: s += U"[LineComment: "; break;
        case GroupBegin: s += U"[GroupBegin]\n"; return s;
        case GroupEnd: s += U"[GroupEnd]\n"; return s;
    }
    s += Escape(token.string_content) + U"]\n";
    return s;
}

String Node::Str() const {
    return StringiseType(*this);
}
} // namespace TeX
