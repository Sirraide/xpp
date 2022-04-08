#include "parser.h"

#include <filesystem>
#include <fmt/format.h>
#include <variant>
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

Macro::Macro(NodeList _replacement) : replacement(std::move(_replacement)) {}
Macro::Macro(std::vector<NodeList> _delimiters, NodeList _replacement)
    : replacement(std::move(_replacement)), delimiters(std::move(_delimiters)) {}

Parser::Parser(const options::parsed_options& _opts) : LexerBase(_opts.get<"-f">()), opts(_opts) {
    if (opts.has<"-o">()) output_file = fopen(opts.get<"-o">().c_str(), "w");
    else output_file = stdout;
    if (!output_file) Die("Could not open output file: %s", strerror(errno));

    Parser::NextChar();
    Parser::NextToken();
    if (opts.has<"--print-tokens">()) {
        Parser::PrintAllTokens(output_file);
        exit(0);
    } else if (opts.has<"--wc">()) {
        U64         chars{};
        U64         words = 1;
        std::string text;
        do {
            if (token.type == T::Text || token.type == T::Whitespace) {
                chars++;
                if (token.type == T::Whitespace) words++;
            }
            NextToken();
        } while (token.type != T::EndOfFile);
        std::cout << "Number of characters: " << chars << "\n";
        std::cout << "Number of words:      " << words << "\n";
        exit(0);
    }
    if (opts.has<"--format">()) {
        Parser::Format();
        exit(0);
    }
    Parser::Parse();
    if (!has_error) Parser::Emit();
}

/// Break the input into lines.
auto Parser::FormatPass1() -> std::string {
    std::string output;
    U64         col{};
    U64         line   = 1;
    bool        has_ws = false;

    struct loc {
        U64 line;
        U64 offset;
    };

    /// Keep track of the number of "{" and "}" after \end.
    /// Break once it's 0.
    U64 env_end_arg_depth = 0;

    /// This holds the line and column numbers of \begin elements.
    /// This is used to check whether the \begin and \end elements that belong
    /// together are on a single line or not.
    std::stack<loc> begin_stack{};
    while (token.type != TokenType::EndOfFile) {
        if (token.type != T::Whitespace) has_ws = false;
        switch (token.type) {
            case T::EndOfFile:
            case T::Invalid: Die("Invalid token");
            case T::Text:
                output += ToUTF8(token.string_content);
                col++;
                break;
            case T::CommandSequence:
                if (token.string_content == U"\\item" && col != 0) {
                    col = 0;
                    output += "\n";
                    line++;
                } else if (token.string_content == U"\\begin") {
                    begin_stack.push({line, output.size()});
                } else if (token.string_content == U"\\end") {
                    /// Check if we have a \begin on the stack.
                    if (!begin_stack.empty()) {
                        auto [b_line, b_offset] = begin_stack.top();
                        begin_stack.pop();
                        /// If the \begin and \end are not on the same line, insert a line break
                        /// before the \begin and \end if they're not already on a new line.
                        if (b_line != line) {
                            if (b_offset) output.insert(b_offset, "\n");
                            if (col != 0) {
                                col = 0;
                                output += "\n";
                                line++;
                            }

                            /// Append \end.
                            col += 4;
                            output += "\\end";
                            NextToken(); /// Yeet \end.

                            /// Check the next token to see if it's "{".
                            if (token.type == T::GroupBegin) {
                                col++;
                                output += '{';
                                env_end_arg_depth++;
                            } else PushLookahead(token);
                            break; /// This will yeet the "{" at the end of the loop.
                        }
                    }

                    /// Otherwise, just append \end.
                    col += token.string_content.size();
                    output += ToUTF8(token.string_content);
                    break;
                }

                col += token.string_content.size();
                output += ToUTF8(token.string_content);
                if (token.string_content == U"\\\\" || token.string_content == U"\\hline") {
                    col = 0;
                    NextToken();
                    if (token.type == T::CommandSequence && (token.string_content == U"\\hline" || token.string_content == U"\\cline"))
                        output += ToUTF8(token.string_content);
                    else PushLookahead(token);
                    output += "\n";
                    line++;
                }
                break;
            case T::Macro:
            case T::MacroArg:
                col += token.string_content.size();
                output += ToUTF8(token.string_content);
                break;
            case T::LineComment:
                col = 0;
                output += ToUTF8(token.string_content);
                break;
            case T::Whitespace: {
                /// Count the number of newlines.
                U64 newlines{};
                for (auto c : token.string_content)
                    if (c == '\n' && ++newlines == 2)
                        break;
                /// Two or more newlines are a paragraph break.
                /// One is just whitespace.
                if (newlines == 2) {
                    output += "\n\n";
                    line += 2;
                    col = 0;
                } else if (col > 100) {
                    output += "\n";
                    line++;
                    has_ws = true;
                    col    = 0;
                } else {
                    if (!has_ws && col != 0) output += " ";
                    has_ws = true;
                    col += col != 0;
                    break;
                }
            } break;
            case T::GroupBegin:
                if (env_end_arg_depth) env_end_arg_depth++;
                output += "{";
                col++;
                break;
            case T::GroupEnd:
                output += "}";
                col++;
                if (env_end_arg_depth) {
                    env_end_arg_depth--;
                    if (!env_end_arg_depth) {
                        output += '\n';
                        col = 0;
                        line++;
                    }
                }
                break;
        }
        NextToken();
    }
    if (output[output.size() - 1] != '\n') output += "\n";
    return output;
}

/// Trim and indent the lines.
void Parser::FormatPass2(std::string&& text) {
    std::vector<std::string> lines;
    U64                      pos{};
    for (;;) {
        auto nl = text.find('\n', pos);
        lines.push_back(text.substr(pos, nl - pos));
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    for (auto& item : lines) item = Trim(item);

    I64  indent_lvl{};
    auto indent_by = [&](std::string& s, I64 how_much) {
        std::string t;
        for (I64 i = 0; i < how_much; i++) t += ' ';
        if (!t.empty()) s = t + s;
    };
    auto indent = [&](std::string& s) { indent_by(s, indent_lvl); };
    for (auto& item : lines) {
        /// \item is a special case.
        bool is_item = false;

        /// We might want to start indenting on the next line instead of this one.
        I64 afterindent{};

        /// \begin and \end change the indentation by 4; as do \if* and \fi.
        /// Environments that contain \item's change the indentation by 6.
        if (item.starts_with("\\begin") || item.starts_with("\\if")) {
            if (item.starts_with("\\begin{enumerate}") || item.starts_with("\\begin{itemize}")) afterindent = 10;
            else afterindent = 4;
        } else if (item.starts_with("\\end") || (item.size() == 3 && item == "\\fi") || (item.starts_with("\\fi "))) {
            if (item.starts_with("\\end{enumerate}") || item.starts_with("\\end{itemize}")) indent_lvl -= 6;
            if (indent_lvl < 4) indent_lvl = 0;
            else indent_lvl -= 4;
        } else if (item.starts_with("\\item")) is_item = true;

        /// A different number of { and } on a line changes the indentation.
        I64 lbra_cnt{}, rbra_cnt{};
        pos = 0;
        while (pos = item.find('{', pos), pos != std::string::npos) {
            lbra_cnt++;
            pos++;
        }
        pos = 0;
        while (pos = item.find('}', pos), pos != std::string::npos) {
            rbra_cnt++;
            pos++;
        }

        /// Unindent due to more } than {.
        if (lbra_cnt < rbra_cnt) {
            I64 diff = (rbra_cnt - lbra_cnt) * 4;
            if (indent_lvl < diff) indent_lvl = 0;
            else indent_lvl -= diff;
        }

        // Handle indentation for \item.
        if (is_item) indent_by(item, indent_lvl < 6 ? 0 : indent_lvl - 6);
        else indent(item);

        /// Indent before the next line
        if (afterindent) indent_lvl += afterindent;

        /// Indent due to more { than }.
        if (lbra_cnt > rbra_cnt) indent_lvl += (lbra_cnt - rbra_cnt) * 4;
    }

    for (auto& item : lines) std::cout << item << "\n";
}

void Parser::Format() {
    FormatPass2(FormatPass1());
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

String Trim(const String& tstring) {
    U64 start = 0, end = tstring.size() - 1;
    while (start < end && IsSpace(tstring[start])) start++;
    while (end > start && IsSpace(tstring[end])) end--;
    return tstring.substr(start, end - start + !IsSpace(tstring[end]));
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
    if (I32(lastc) == EOF) Die("LexText called at end of file");
    if (IsSpace(lastc)) {
        token.type = TokenType::Whitespace;
        do {
            token.string_content += lastc;
            NextChar();
        } while (IsSpace(lastc));
        return;
    }

    token.type           = TokenType::Text;
    token.string_content = lastc;
    NextChar();
}

void Parser::NextToken() {
    if (!lookahead_queue.empty()) {
        token = std::move(lookahead_queue.front());
        lookahead_queue.pop();
        return;
    }

    token     = Token();
    token.loc = Here();

    if (at_eof) {
        token.type = TokenType::EndOfFile;
        return;
    }

    if (I32(lastc) == EOF) Die("NextToken: at_eof not set at end of file!");

    token.type = TokenType(lastc);
    switch (lastc) {
        case U'%': return LexLineComment();
        case U'\\': return LexCommandSequence();
        case U'{':
        case U'}':
            NextChar();
            return;
        case U'#':
            LexMacroArg();
            return;
        default:
            token.type = TokenType::Text;
            LexText();
    }
}

void Parser::NextNonWhitespaceToken() {
    do NextToken();
    while (token.type == TokenType::Whitespace);
}

void Parser::SkipCharsUntilIfWhitespace(Char c) {
    auto here = Here();
    while (!at_eof && lastc != c && IsSpace(lastc)) NextChar();
    if (at_eof) Fatal(here, "End of file reached while looking for character %lc", wchar_t(c));
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
    SkipCharsUntilIfWhitespace('{');
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
        SkipCharsUntilIfWhitespace('{');
        if (lastc != '{') LEXER_ERROR("Syntax of \\Replace* is \\Replace*{text}{replacement}");
        NextChar(); /// yeet '{'

        String text = ReplaceReadUntilBrace();
        if (at_eof) LEXER_ERROR("Syntax of \\Replace* is \\Replace*{text}{replacement}");
        NextChar(); /// yeet '}'

        SkipCharsUntilIfWhitespace('{');
        if (lastc != '{') LEXER_ERROR("Syntax of \\Replace* is \\Replace*{text}{replacement}");
        NextChar(); /// yeet '{'

        String replacement = ReplaceReadUntilBrace();
        if (at_eof) LEXER_ERROR("Syntax of \\Replace* is \\Replace*{text}{replacement}");
        NextChar(); /// yeet '}'

        raw_rep_rules.processed.emplace_back(text, replacement);
        NextToken();
    } else {
        NextNonWhitespaceToken(); /// yeet '\Replace'
        auto text        = ParseGroup();
        auto replacement = ParseGroup();
        rep_rules.rules.emplace_back(text, replacement);
    }
}

std::vector<NodeList> Parser::ParseMacroArgs() {
    using enum TokenType;
    std::vector<NodeList> args;
    auto                  here = Here();
    for (;;) {
        NextToken();
        NodeList delimiter;
        while (!at_eof && token.type != GroupBegin && token.type != MacroArg) {
            delimiter.push_back(token);
            NextToken();
        }
        if (at_eof) {
            Error(here, "Macro definition terminated by end of file");
            return {};
        }
        args.push_back(delimiter);
        if (token.type == GroupBegin) return args;
        if (token.type != MacroArg) {
            Error(Here(), "Expected MacroArg or GroupBegin in macro definition, got %s",
                TokenTypeToString(token.type).c_str());
            return {};
        }
    }
}

void Parser::HandleDefine() {
    NextNonWhitespaceToken(); /// yeet '\Define'
    Expect(TokenType::CommandSequence);
    String cs = token.string_content;
    NextNonWhitespaceToken(); /// yeet csname
    if (token.type == TokenType::MacroArg) macros[cs] = {ParseMacroArgs(), ParseGroup()};
    else macros[cs] = ParseGroup();
}

void Parser::ParseCommandSequence() {
    if (token.string_content == U"\\Define") {
        HandleDefine();
    } else if (token.string_content == U"\\Undef") {
        NextNonWhitespaceToken(); /// yeet '\Undef'
        Expect(TokenType::CommandSequence);
        if (macros.contains(token.string_content)) macros.erase(token.string_content);
        NextToken(); /// yeet cs
    } else if (token.string_content == U"\\Replace") {
        HandleReplace();
    } else if (token.string_content == U"\\Include") {
        NextNonWhitespaceToken(); /// yeet '\Include'
        auto group = ParseGroup(true);
        IncludeFile(ToUTF8(Trim(AsTextNode(group))));
        NextToken();
    } else if (macros.contains(token.string_content)) {
        HandleMacroExpansion();
    }
}

void Parser::Emit() {
    ProcessReplacementRules();
    MergeTextNodes(tokens);
    for (auto& m : macros) MergeTextNodes(m.second.replacement);
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
            case Whitespace:
                goto _default;
            case CommandSequence:
                if (macros.contains(node.string_content))
                    Unreachable("ConstructText: Unexpanded macro \'"
                                << ToUTF8(node.string_content) << "\'");
                else processed_text += node.string_content;
                continue;
            case EndOfFile: return;
            case Macro:
                Unreachable("ConstructText: Macro should have been removed from NodeList");
            default:
            _default:
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
            case Whitespace:
            case Text:
                text.append(node.string_content);
                break;
            case CommandSequence:
                if (macros.contains(node.string_content))
                    text.append(AsTextNode(macros[node.string_content].replacement));
                else text.append(node.string_content);
                break;
            default:
                Die("Serialisation of type %s is not implemented", TokenTypeToString(node.type).c_str());
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
String Parser::ReplaceReadUntilBrace() {
    String text;
    while (!at_eof && lastc != U'}') {
        if (lastc == U'\\') {
            NextChar();
            if (at_eof) break;
            switch (lastc) {
                case U'\\':
                case U'{':
                case U'}':
                    text += lastc;
                    break;
                default:
                    text += '\\';
                    text += lastc;
            }
            NextChar();
            continue;
        }
        text += lastc;
        NextChar();
    }
    return text;
}

String StringiseType(const Node& token) {
    using enum TokenType;
    String s;
    s += (String) token.loc;
    s += U": ";
    switch (token.type) {
        case Invalid: s += U"[Invalid: "; break;
        case Whitespace: s += U"[Whitespace: "; break;
        case Text: s += U"[Text: "; break;
        case EndOfFile: s += U"[EndOfFile]\n"; return s;
        case CommandSequence: s += U"[CommandSequence: "; break;
        case Macro: s += U"[Macro: "; break;
        case MacroArg: s += U"[Arg: "; break;
        case LineComment: s += U"[LineComment: "; break;
        case GroupBegin: s += U"[GroupBegin]\n"; return s;
        case GroupEnd: s += U"[GroupEnd]\n"; return s;
    }
    s += Escape(token.string_content) + U"]\n";
    return s;
}

void Parser::Expect(TokenType type) {
    if (token.type != type) Error(Here(),
        "Expected token type %s, but was %s",
        TokenTypeToString(type).c_str(),
        TokenTypeToString(token.type).c_str());
}

std::string Parser::TokenTypeToString(TokenType type) {
    using enum TokenType;
    switch (type) {
        case Invalid: return "Invalid";
        case Text: return "Text";
        case EndOfFile: return "EndOfFile";
        case CommandSequence: return "CommandSequence";
        case Macro: return "Macro";
        case MacroArg: return "MacroArg";
        case Whitespace: return "Whitespace";
        case LineComment: return "LineComment";
        case GroupBegin: return "GroupBegin";
        case GroupEnd: return "GroupEnd";
    }
    Unreachable("TokenTypeToString");
}

void Parser::MergeTextNodes(NodeList& lst) {
    using enum TokenType;
    for (U64 i = 0; i < lst.size(); i++) {
        if (lst[i].type == Text || lst[i].type == Whitespace) {
            U64 start = i++;
            if (i == lst.size()) break;
            if (lst[i].type == Text || lst[i].type == Whitespace) {
                Token node;
                node.type           = Text;
                node.string_content = lst[i - 1].string_content;
                do node.string_content += lst[i++].string_content;
                while (i < lst.size() && (lst[i].type == Text || lst[i].type == Whitespace));
                lst.erase(lst.begin() + I64(start), lst.begin() + I64(i));
                lst.insert(lst.begin() + I64(start), node);
                i = start;
            }
        }
    }
}

void Parser::LexMacroArg() {
    NextChar(); /// yeet '#'
    U64 arg_code = 0;
    if (at_eof) LEXER_ERROR("Eof reached while parsing macro argument");
    if (lastc == '#') {
        arg_code = 10;
        NextChar(); /// yeet second '#'
        if (at_eof) LEXER_ERROR("Eof reached while parsing macro argument");
    }
    I64 num = ToDecimal(lastc);
    if (num < 1) LEXER_ERROR("Expected number after # to be between 1 and 9");
    NextChar(); /// yeet number

    arg_code += U64(num);
    token.type   = TokenType::MacroArg;
    token.number = arg_code;
}

void Parser::HandleMacroExpansion() {
    const auto&           macro = macros[token.string_content];
    auto                  here  = Here();
    std::vector<NodeList> args;
    NextToken(); /// yeet the macro name
    for (const auto& delim : macro.delimiters) {
        if (delim.empty()) {
            args.push_back({token});
            NextToken(); /// yeet token
        } else {
            NodeList arg;
            for (U64 i = 0, sz = delim.size(); i < sz; i++) {
                auto& d_token = delim[i];
                while (!at_eof && token != d_token) {
                    arg.push_back(token);
                    NextToken(); /// yeet token
                }
                if (at_eof) {
                    Error(here, "Eof reached while parsing macro arguments");
                    return;
                }
                NodeList saved_delim_tokens;
                do {
                    saved_delim_tokens.push_back(token);
                    NextToken();
                    i++;
                } while (!at_eof && i < sz && token == d_token);
                if (i == sz) goto next_delim;
                if (at_eof) {
                    Error(here, "Eof reached while parsing macro arguments");
                    return;
                }
                arg.insert(arg.end(), saved_delim_tokens.begin(), saved_delim_tokens.end());
            }
        next_delim:
            args.push_back(arg);
        }
    }

    for (const auto& tok : macro.replacement) {
        if (tok.type == TokenType::MacroArg) {
            const U64 offset = tok.number % 10 - 1;
            if (offset >= args.size())
                Fatal(here, "Macro arg index too big: %zu; size was: %zu", offset, args.size());
            for (const auto& node : args[offset]) PushLookahead(node);
        } else PushLookahead(tok);
    }
}

void Parser::PushLookahead(const Node& node) {
    lookahead_queue.push(node);
    if (token.type == TokenType::EndOfFile) NextToken();
}

String Node::Str() const {
    return StringiseType(*this);
}
} // namespace TeX
