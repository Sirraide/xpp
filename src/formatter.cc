#include "parser.h"
namespace TeX {

/// Format Pass 1: Break the input into lines.
auto Parser::FormatPass1(NodeList&& tokens, U64 line_width) -> std::string {
    struct loc {
        U64 line;
        U64 offset;
    };

    struct def {
        U64 line;
        U64 offset;
        U64 open_braces;
    };

    /// Buffer where we're going to store the result of pass 1.
    std::string output;

    /// This is used to make sure that we don't insert any more
    /// whitespace if we've already inserted whitespace.
    bool has_ws = false;

    /// This is NOT an exact line count, but it can
    /// serve to determine whether two tokens are on
    /// the same line or not.
    U64 line = 1;

    /// This serves to keep lines < line_width chars.
    U64 col{};

    /// Keep track of the number of "{" and "}" after \end.
    /// Break once it's 0.
    U64 env_end_arg_depth{};

    /// Offset to the last space we inserted.
    /// Used to insert a line break if an element is too long.
    I64 last_ws_offset{};

    /// This holds the line and column numbers of \def elements.
    /// This is used to check whether the \def and "}" elements that belong
    /// together are on a single line or not.
    std::stack<def> def_stack{};

    /// Used for formatting \if ... \fi.
    std::stack<loc> if_stack{};

    /// This holds the line and column numbers of \begin elements.
    /// This is used to check whether the \begin and \end elements that belong
    /// together are on a single line or not.
    std::stack<loc> begin_stack{};

    /// Loop variable.
    /// This is declared here so that we can capture it
    /// in the lambdas below.
    U64 tok_index{};

    /// Set this to false if the current token should not be discarded
    /// at the end of the loop. This resets every iteration.
    bool discard = true;

    /// Whether the last token was a command sequence or "}".
    /// This is used to check if we should preserve line breaks.
    bool last_was_seq_or_gr_end = false;

    /// Whether to insert a line break if the next token is not text.
    bool break_if_not_text = false;

    /// Advance to the next token.
    auto Next = [&] { tok_index++; };

    /// Check if we're at the end of the input.
    auto AtEnd = [&] { return tok_index == tokens.size(); };

    /// Append a line break to the output
    auto Nl = [&] {
        col = 0;
        output += "\n";
        last_ws_offset = 0;
        line++;
    };

    /// Append a space to the output.
    auto Space = [&] {
        if (col != 0) {
            last_ws_offset = I64(output.size());
            output += " ";
            has_ws = true;
            col++;
        }
    };

    /// Count the number of line breaks in the current token.
    /// Since in LaTeX, more than two line breaks is the same as two line breaks,
    /// we stop searching after finding two.
    auto TokenNewlines = [](const Token& tok) {
        U64 newlines{};
        for (auto c : tok.string_content)
            if (c == '\n' && ++newlines == 2)
                break;
        return newlines;
    };

    /// Append a line break to the output and yeet the next token if it's a line break.
    auto ProvideNl = [&] [[nodiscard]] {
        Next();
        if (AtEnd()) return false;

        /// If the next token is a comment, print it before trying to insert a newline.
        /// This allows the user to put comments after a closing "}".
        if (tokens[tok_index].type == T::LineComment) {
            auto comment_str = ToUTF8(tokens[tok_index].string_content);
            output.append(comment_str.c_str(), comment_str.size() < 2 ? comment_str.size() : comment_str.size() - 1);
            Next();
            if (AtEnd()) return false;
        }

        discard = tokens[tok_index].type == T::Whitespace && TokenNewlines(tokens[tok_index]) == 1;
        Nl();
        return true;
    };

    auto FormatEnvBegin = [&] {
        /// Push this onto the stack and append the \begin.
        begin_stack.push({line, output.size()});
        output += "\\begin";
        col += 6;
        Next(); /// Yeet \begin.
        if (AtEnd()) return;

        /// \begin{document} must be on a separate line.
        /// "{" "document" "}"
        if (tokens[tok_index].type != TokenType::GroupBegin) {
            discard = false;
            return;
        }
        output += '{';
        col++;
        Next(); /// Yeet "{".
        if (AtEnd()) return;

        /// "document" "}"
        if (tokens[tok_index].type != TokenType::Text || tokens[tok_index].string_content != U"document") {
            discard = false;
            return;
        }
        output += "document";
        col += 8;

        Next(); /// Yeet "document".
        if (AtEnd()) return;

        /// "}"
        if (tokens[tok_index].type != TokenType::GroupEnd) {
            discard = false;
            return;
        }
        output += '}';
        Nl();

        Next(); /// Yeet "}"
        if (AtEnd()) return;

        /// Yeet the next whitespace token.
        discard = tokens[tok_index].type == TokenType::Whitespace && TokenNewlines(tokens[tok_index]) == 1;
    };

    auto FormatEnvEnd = [&] {
        /// Check if we have a \begin on the stack.
        if (!begin_stack.empty()) {
            auto [b_line, b_offset] = begin_stack.top();
            begin_stack.pop();
            /// If the \begin and \end are not on the same line, insert a line break
            /// before the \begin and \end if they're not already on a new line.
            if (b_line != line) {
                if (b_offset && output[b_offset - 1] != '\n') output.insert(b_offset, "\n");
                if (col != 0) Nl();

                /// Append \end.
                col += 4;
                output += "\\end";
                Next(); /// Yeet \end.
                if (AtEnd()) return;

                /// Check the next token to see if it's "{".
                if (tokens[tok_index].type == T::GroupBegin) {
                    col++;
                    output += '{';
                    env_end_arg_depth++;
                    Next(); /// Yeet "{".
                }
                discard = false;
                return;
            }
        }

        /// Otherwise, just append \end.
        col += tokens[tok_index].string_content.size();
        output += ToUTF8(tokens[tok_index].string_content);
    };

    while (tok_index < tokens.size()) {
        discard = true;
        if (tokens[tok_index].type != T::Whitespace) has_ws = false;
        if (break_if_not_text) {
            if (tokens[tok_index].type != T::Text) Nl();
            break_if_not_text = false;
        }
        switch (tokens[tok_index].type) {
            case T::EndOfFile:
            case T::Invalid: Die("Invalid token");
            case T::Text:
                output += ToUTF8(tokens[tok_index].string_content);
                col += tokens[tok_index].string_content.size();
                break;
            case T::MacroArg: {
                std::string arg{"#"};
                auto num = tokens[tok_index].number;
                if (num >= 10) {
                    num -= 10;
                    arg += "#";
                }

                arg += std::to_string(num);
                col += arg.size();
                output += arg;
            } break;
            case T::CommandSequence:
            case T::Macro:
                if (const auto& s = tokens[tok_index].string_content; s == U"\\item" && col != 0) {
                    Nl();
                } else if (s == U"\\begin") {
                    FormatEnvBegin();
                    break;
                } else if (s == U"\\def" || s == U"\\Define" || s == U"\\Defun" || s == U"\\Eval") {
                    def_stack.push({line, output.size(), 0});
                } else if (s == U"\\end") {
                    FormatEnvEnd();
                    break;
                } else if (s.starts_with(U"\\if"))
                    if_stack.push({line, output.size()});
                else if (s == U"\\fi") {
                    if (!if_stack.empty()) {
                        auto [if_line, if_offset] = if_stack.top();
                        if_stack.pop();
                        if (if_offset && output[if_offset - 1] != '\n') output.insert(if_offset, "\n");
                        if (col != 0) Nl();
                        output += ToUTF8(s);
                        col += 3;
                        break;
                    }
                } else if (s == U"\\[") {
                    if (col != 0) Nl();
                } else if (s == U"\\]") {
                    col += tokens[tok_index].string_content.size();
                    output += ToUTF8(tokens[tok_index].string_content);
                    (void) ProvideNl();
                    break;
                }

                col += tokens[tok_index].string_content.size();
                output += ToUTF8(tokens[tok_index].string_content);

                /// "\ " at the end of a line
                if (tokens[tok_index].string_content.ends_with(U"\n")) {
                    line++;
                    col            = 0;
                    last_ws_offset = 0;
                }

                if (tokens[tok_index].string_content == U"\\\\"
                    || tokens[tok_index].string_content == U"\\hline"
                    || tokens[tok_index].string_content == U"\\cline") {
                    Next();
                    if (AtEnd()) break;
                    /// Keep \hline and \cline on the same line as \\.
                    while (tokens[tok_index].type == T::CommandSequence
                           && (tokens[tok_index].string_content == U"\\hline" || tokens[tok_index].string_content == U"\\cline")) {
                        output += ToUTF8(tokens[tok_index].string_content);
                        Next();
                        if (AtEnd()) goto done;
                    }
                    discard = tokens[tok_index].type == T::Whitespace && TokenNewlines(tokens[tok_index]) == 1;
                    Nl();
                }
            done:
                break;
            case T::LineComment:
                col = 0;
                line++;
                output += ToUTF8(tokens[tok_index].string_content);
                break;
            case T::Whitespace: {
                /// Count the number of newlines.
                U64 newlines = TokenNewlines(tokens[tok_index]);

                /// Two or more newlines are a paragraph break.
                /// One is just whitespace.
                if (newlines == 2) {
                    if (col > line_width && last_ws_offset > 0) output.replace(U64(last_ws_offset), 1, "\n");
                    output += '\n';
                    Nl();
                } else if (col > line_width) {
                    /// Reflow the line if we can.
                    if (last_ws_offset > 0) {
                        output.replace(U64(last_ws_offset), 1, "\n");
                        col = output.size() - U64(last_ws_offset) - 1;
                    }
                    /// The line might still be too long.
                    if (col > line_width) Nl();
                    else {
                        /// If the user inserted a line break here and the next token is not text,
                        /// keep the line break. Otherwise, replace it with a space to reflow the text.
                        if (newlines == 1) break_if_not_text = true;
                        Space();
                    }
                } else {
                    /// If the last token was a command sequence or "}", and
                    /// this is a manual line break, keep the line break.
                    /// Otherwise, convert it to a single space.
                    if (last_was_seq_or_gr_end && newlines) Nl();
                    else if (!has_ws && col != 0) {
                        /// If the user inserted a line break here and the next token is not text,
                        /// keep the line break. Otherwise, replace it with a space to reflow the text.
                        if (newlines == 1) break_if_not_text = true;
                        Space();
                    }
                    break;
                }
            } break;
            case T::GroupBegin:
                if (env_end_arg_depth) env_end_arg_depth++;
                if (!def_stack.empty()) {
                    def_stack.top().open_braces++;
                    /// Insert a line break after the "{" of a \def if the
                    /// user provided one.
                    if (def_stack.top().open_braces == 1) {
                        output += "{";
                        Next(); /// Yeet "{"
                        if (AtEnd()) break;

                        if (tokens[tok_index].type == T::Whitespace) {
                            U64 newlines = TokenNewlines(tokens[tok_index]);
                            if (newlines >= 1) {
                                if (newlines > 1) output += '\n';
                                Nl();
                                break; /// Yeet whitespace.
                            }
                        }

                        discard = false;
                        break;
                    }
                }
                output += "{";
                col++;
                break;
            case T::GroupEnd:
                /// If this "}" closes a \def, insert a line before the def
                /// as well as before and after this if the \def is not on the
                /// same line as this.
                if (!def_stack.empty()) {
                    def_stack.top().open_braces--;
                    if (!def_stack.top().open_braces) {
                        auto [d_line, d_offset, _] = def_stack.top();
                        def_stack.pop();
                        if (d_line != line) {
                            /// Insert a line break before the \def and after the "{".
                            if (d_offset && output[d_offset - 1] != '\n') output.insert(d_offset, "\n");
                            if (col != 0) Nl();
                            output += '}';
                            (void) ProvideNl();
                            break;
                        }
                    }
                }
                output += "}";
                col++;
                if (env_end_arg_depth) {
                    env_end_arg_depth--;
                    if (!env_end_arg_depth) (void) ProvideNl();
                }
                break;
        }
        last_was_seq_or_gr_end = tokens[tok_index].type == T::CommandSequence || tokens[tok_index].type == T::GroupEnd;
        if (discard) tok_index++;
    }
    if (output[output.size() - 1] != '\n') output += "\n";
    return output;
}

/// Format Pass 2: Trim whitespace and indent the lines.
auto Parser::FormatPass2(std::string&& text, std::vector<std::string> enumerate_envs) -> std::vector<std::string> {
    std::vector<std::string> enumerate_envs_begin;
    std::vector<std::string> enumerate_envs_end;

    for (auto& env : enumerate_envs) {
        enumerate_envs_begin.push_back("\\begin{" + env + "}");
        enumerate_envs_end.push_back("\\end{" + env + "}");
    }

    /// Split the input into lines.
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
            bool enumerate = false;
            for (const auto& env : enumerate_envs_begin) {
                if (item.starts_with(env)) {
                    enumerate = true;
                    afterindent = 10;
                    break;
                }
            }

            if (!enumerate && !item.starts_with("\\begin{document}")) afterindent = 4;
        } else if (item.starts_with("\\end") || (item.starts_with("\\fi") && (item.size() == 3 || !std::isalpha(item[3])))) {
            for (const auto& env : enumerate_envs_end) {
                if (item.starts_with(env)) {
                    indent_lvl -= 6;
                    break;
                }
            }
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

        /// Handle indentation for \item.
        if (is_item) indent_by(item, indent_lvl < 6 ? 0 : indent_lvl - 6);
        /// Unindent this line if it starts with "}"
        else if (item.starts_with("}")) {
            I64 new_indent = indent_lvl - (rbra_cnt - lbra_cnt) * 4;
            indent_by(item, new_indent < 0 ? 0 : new_indent);
        }
        /// Just indent it.
        else
            indent(item);

        /// Indent before the next line
        if (afterindent) indent_lvl += afterindent;

        /// Unindent if more } than {,
        /// or indent if to more { than }.
        if (lbra_cnt < rbra_cnt) {
            I64 diff = (rbra_cnt - lbra_cnt) * 4;
            if (indent_lvl < diff) indent_lvl = 0;
            else indent_lvl -= diff;
        } else if (lbra_cnt > rbra_cnt) indent_lvl += (lbra_cnt - rbra_cnt) * 4;
    }

    /// Remove consecutive empty lines.
    std::vector<I64> empty_lines;
    bool             prev_was_empty = false;
    for (U64 i = 0; i < lines.size(); i++) {
        if (lines[i].empty()) {
            if (prev_was_empty) empty_lines.push_back(I64(i));
            else prev_was_empty = true;
        } else prev_was_empty = false;
    }
    for (auto it = empty_lines.rbegin(); it != empty_lines.rend(); ++it)
        lines.erase(lines.begin() + *it);

    return lines;
}

void Parser::Format() {
    /// Split the text into tokens and merge text nodes.
    while (token.type != T::EndOfFile) {
        tokens.push_back(token);
        NextToken();
    }
    MergeTextNodes(tokens, false);

    /// List of environments that should be indented like enumerate.
    std::vector<std::string> enumerate_envs = {"enumerate", "itemize"};
    if (auto envs = options::get<"--enumerate-env">()) {
        enumerate_envs.insert(enumerate_envs.end(), envs->begin(), envs->end());
    }

    for (auto& item : FormatPass2(FormatPass1(std::move(tokens), line_width), std::move(enumerate_envs)))
        fprintf(output_file, "%s\n", item.c_str());
}

} // namespace TeX