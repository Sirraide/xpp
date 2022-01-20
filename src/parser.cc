#include "parser.h"

#include <filesystem>
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
    if (!has_error) Parser::Emit();
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
    if (!lookahead_queue.empty()) {
        at_eof = false;
        token  = std::move(lookahead_queue.front());
        lookahead_queue.pop();
        return;
    }

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

NodeList Parser::ParseGroup(bool keep_closing_brace) {
    Expect(TokenType::GroupBegin);
    group_count++;
    auto here = Here();
    NextToken(); /// yeet '{'

    if (at_eof) Error(here, "Group terminated by end of file");

    NodeList lst;
    U64      depth = group_count;
    while (!at_eof) {
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
    if (!keep_closing_brace) NextToken(); /// yeet '}'

    return lst;
}

void Parser::HandleReplace() {
    if (!at_eof && lastc == U'*') {
        NextChar(); /// yeet '*'
        if (at_eof) LEXER_ERROR("Unterminated \\Replace*");
        if (lastc != '|') LEXER_ERROR("Syntax of \\Replace* is \\Replace*|text|replacement|");
        NextChar(); /// yeet '|'
        String text = ReadUntilChar(U'|');
        if (at_eof) LEXER_ERROR("Syntax of \\Replace* is \\Replace*|text|replacement|");
        NextChar(); /// yeet '|'
        String replacement = ReadUntilChar(U'|');
        if (at_eof) LEXER_ERROR("Syntax of \\Replace* is \\Replace*|text|replacement|");
        NextChar(); /// yeet '|'
        raw_rep_rules.processed.emplace_back(text, replacement);
        NextToken();
    } else {
        NextToken(); /// yeet '\Replace'
        auto text        = ParseGroup();
        auto replacement = ParseGroup();
        rep_rules.rules.emplace_back(text, replacement);
    }
}

void Parser::ParseCommandSequence() {
    if (token.string_content == U"\\Define") {
        NextToken(); /// yeet '\Define'
        Expect(TokenType::CommandSequence);
        String cs = token.string_content;
        NextToken(); /// yeet csname
        auto group = ParseGroup();
        macros[cs] = group;
    } else if (token.string_content == U"\\Undef") {
        NextToken(); /// yeet '\Undef'
        Expect(TokenType::CommandSequence);
        if (macros.contains(token.string_content)) macros.erase(token.string_content);
        NextToken(); /// yeet cs
    } else if (token.string_content == U"\\Replace") {
        HandleReplace();
    } else if (token.string_content == U"\\Include") {
        NextToken(); /// yeet '\Include'
        auto group = ParseGroup(true);
        IncludeFile(ToUTF8(AsTextNode(group)));
        NextToken();
    } else if (macros.contains(token.string_content)) {
        for (const auto& tok : macros[token.string_content])
            lookahead_queue.push(tok);
        NextToken(); /// yeet expanded macro
    }
}

void Parser::Expect(TokenType type) {
    if (token.type != type) Error(Here(), "Expected token type %d, but was %d", int(type), int(token.type));
}

void Parser::Emit() {
    ProcessReplacementRules();
    ProcessReplacement(tokens);
    ConstructText(tokens);
    ApplyRawReplacementRules();
    fmt::print(output_file, "{}", ToUTF8(processed_text));
}

void Parser::ConstructText(NodeList& nodes) {
    using enum TokenType;
    for (auto& node : nodes) {
        switch (node.type) {
            case GroupBegin:
                processed_text += U'{';
                break;
            case GroupEnd:
                processed_text += U'}';
                break;
            case CommandSequence:
                if (macros.contains(node.string_content))
                    Unreachable("ConstructText: Unexpanded macro \'"
                                << ToUTF8(node.string_content) << "\'");
                // ConstructText(macros[node.string_content]);
                else processed_text += node.string_content;
                break;
            case Macro:
                Unreachable("ConstructText: Macro should have been removed from NodeList");
            default:
                processed_text += node.string_content;
        }
    }
}

void Parser::ApplyReplacementRules(String& str) {
    for (const auto& [text, replacement] : rep_rules.processed)
        ReplaceAll(str, text, replacement);
}

void Parser::ApplyRawReplacementRules() {
    for (const auto& [text, replacement] : raw_rep_rules.processed)
        ReplaceAll(processed_text, text, replacement);
}

void Parser::ProcessReplacement(NodeList& nodes) {
    using enum TokenType;
    for (auto& node : nodes) {
        switch (node.type) {
            case Text:
                ApplyReplacementRules(node.string_content);
            default:
                continue;
        }
    }
}

String Parser::AsTextNode(const NodeList& lst) {
    using enum TokenType;
    String text;
    for (const auto& node : lst) {
        switch (node.type) {
            case Text:
                text.append(node.string_content);
                break;
            case CommandSequence:
                if (macros.contains(node.string_content))
                    text.append(AsTextNode(macros[node.string_content]));
                else goto _default;
                break;
            default:
            _default:
                Die("Serialisation of type %d is not implemented", int(node.type));
        }
    }
    return text;
}

void Parser::ProcessReplacementRules() {
    for (const auto& [text, replacement] : rep_rules.rules)
        rep_rules.processed.emplace_back(AsTextNode(text), AsTextNode(replacement));
    for (const auto& [text, replacement] : raw_rep_rules.rules)
        raw_rep_rules.processed.emplace_back(AsTextNode(text), AsTextNode(replacement));
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
