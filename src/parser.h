#ifndef XPP_PARSER_H
#define XPP_PARSER_H

#include "../clopts/include/clopts.hh"

#include <map>
#include <queue>
#include <string>
#include <utils/parser.h>

namespace TeX {
/*struct AST {
};*/

enum struct TokenType : Char {
    Invalid,
    Text,
    EndOfFile       = Eof,
    CommandSequence = U'\\',
    Macro,
    MacroArg,
    Whitespace,
    LineComment = U'%',
    GroupBegin  = U'{',
    GroupEnd    = U'}',
};

struct Node : public TokenBase<char32_t, TokenType, SourceLocationBase<>> {
    String Str() const override;
};

using NodeList      = std::vector<Node>;
using AbstractLexer = LexerBase<FileBase<>, Node>;

struct ReplacementRules {
    std::vector<std::pair<NodeList, NodeList>> rules;
    std::vector<std::pair<String, String>>     processed;
};

struct Macro {
    NodeList              replacement;
    std::vector<NodeList> delimiters;

    Macro() = default;
    Macro(NodeList replacement);
    Macro(std::vector<NodeList> delimiters, NodeList replacement);
};

namespace cl = command_line_options;
struct Parser : public AbstractLexer {
    using options = cl::clopts<
        cl::option<"-o", "The file to output to">,
        cl::option<"-f", "The file to process", std::string, true>,
        cl::flag<"--print-tokens", "Print all tokens to stdout and exit">,
        cl::flag<"--wc", "Count the number of characters and words in the file">,
        cl::flag<"--format", "Format a file instead of preprocessing it">,
        cl::help>;
    const options::parsed_options opts;

    explicit Parser(const options::parsed_options& _opts);
    FILE*                   output_file;
    std::map<String, Macro> macros;
    ReplacementRules        rep_rules;
    ReplacementRules        raw_rep_rules;
    NodeList                tokens;
    U64                     group_count = 0;
    std::queue<Node>        lookahead_queue;
    String                  processed_text;

    void ApplyReplacementRules(String& str);
    void ApplyRawReplacementRules();
    auto AsTextNode(const NodeList& lst) -> String;
    void ConstructText(NodeList& nodes);
    void Emit();
    void Expect(TokenType type);
    void Format();
    void HandleDefine();
    void HandleMacroExpansion();
    void HandleReplace();
    void LexCommandSequence();
    void LexLineComment();
    void LexMacroArg();
    void LexText();
    void NextNonWhitespaceToken();
    void NextToken() override;
    void Parse();
    void ParseCommandSequence();
    auto ParseGroup(bool keep_closing_brace = false) -> NodeList;
    auto ParseMacroArgs() -> std::vector<NodeList>;
    void ParseSequence();
    void ProcessReplacement(NodeList& lst);
    void ProcessReplacementRules();
    void PushLookahead(const Node& node);
    auto ReplaceReadUntilBrace() -> String;
    void SkipCharsUntilIfWhitespace(Char c);

    static void MergeTextNodes(NodeList& lst);
    static auto TokenTypeToString(TokenType type) -> std::string;
};

String StringiseType(const Node& token);

} // namespace TeX

#endif // XPP_PARSER_H
