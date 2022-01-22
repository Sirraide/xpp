#ifndef XPP_PARSER_H
#define XPP_PARSER_H

#include <map>
#include <queue>
#include <string>
#include <utils/clopts.h>
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

struct Parser : public AbstractLexer {
    const Clopts& opts;
    explicit Parser(const Clopts& opts);
    FILE*                   output_file;
    std::map<String, Macro> macros;
    ReplacementRules        rep_rules;
    ReplacementRules        raw_rep_rules;
    NodeList                tokens;
    U64                     group_count = 0;
    std::queue<Node>        lookahead_queue;
    String                  processed_text;

    String                AsTextNode(const NodeList& lst);
    void                  ApplyReplacementRules(String& str);
    void                  ApplyRawReplacementRules();
    void                  ConstructText(NodeList& nodes);
    void                  Emit();
    void                  Expect(TokenType type);
    void                  HandleDefine();
    void                  HandleMacroExpansion();
    void                  HandleReplace();
    void                  LexCommandSequence();
    void                  LexLineComment();
    void                  LexMacroArg();
    void                  LexText();
    static void           MergeTextNodes(NodeList& lst);
    void                  NextNonWhitespaceToken();
    void                  NextToken() override;
    void                  Parse();
    void                  ParseCommandSequence();
    NodeList              ParseGroup(bool keep_closing_brace = false);
    std::vector<NodeList> ParseMacroArgs();
    void                  ParseSequence();
    void                  ProcessReplacement(NodeList& lst);
    void                  ProcessReplacementRules();
    void                  PushLookahead(const Node& node);
    String                ReplaceReadUntilBrace();
    void                  SkipCharsUntilIfWhitespace(Char c);
    static std::string    TokenTypeToString(TokenType type);
};

String StringiseType(const Node& token);

} // namespace TeX

#endif // XPP_PARSER_H
