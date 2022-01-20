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
    LineComment = U'%',
    GroupBegin  = U'{',
    GroupEnd    = U'}',
};

struct Node : public TokenBase<char32_t, TokenType, SourceLocationBase<>> {
    // std::vector<Node> children;
    String Str() const override;
};

using NodeList      = std::vector<Node>;
using AbstractLexer = LexerBase<FileBase<>, Node>;

struct ReplacementRules {
    std::vector<std::pair<NodeList, NodeList>> rules;
    std::vector<std::pair<String, String>>     processed;
};

struct Parser : public AbstractLexer {
    const Clopts& opts;
    explicit Parser(const Clopts& opts);
    FILE*                      output_file;
    std::map<String, NodeList> macros;
    ReplacementRules           rep_rules;
    ReplacementRules           raw_rep_rules;
    NodeList                   tokens;
    U64                        group_count = 0;
    std::queue<Node>           lookahead_queue;
    String                     processed_text;

    void     LexLineComment();
    void     LexCommandSequence();
    void     LexText();
    void     NextToken();
    void     Parse();
    void     EmitCommandSequence();
    void     Emit();
    void     Expect(TokenType type);
    void     ParseToken();
    NodeList ParseGroup(bool in_macrodef = false);
    void     ParseCommandSequence();
    void     ConstructText(NodeList& nodes);
    void     ParseSequence();
    void     ProcessReplacement(NodeList& lst);
    void     ProcessReplacementRules();
    String   AsTextNode(const NodeList& lst);
    void     ApplyReplacementRules(String& str);
    void     ApplyRawReplacementRules();
};

String StringiseType(const Node& token);

} // namespace TeX

#endif // XPP_PARSER_H
