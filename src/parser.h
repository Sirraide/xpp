#ifndef XPP_PARSER_H
#define XPP_PARSER_H

#include <map>
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
    std::vector<Node> children;
    String Str() const override;
};

using NodeList      = std::vector<Node>;
using AbstractLexer = LexerBase<FileBase<>, Node>;
struct Parser : public AbstractLexer {
    const Clopts& opts;
    explicit Parser(const Clopts& opts);
    FILE*                      output_file;
    std::map<String, NodeList> macros;
    NodeList                   tokens;
    U64                        group_count = 0;

    void LexLineComment();
    void LexCommandSequence();
    void LexText();
    void NextToken();
    // static AST Parse(std::string filename);
    void     Parse();
    void     EmitCommandSequence();
    void     Expect(TokenType type);
    void     ParseToken();
    NodeList ParseGroup(bool in_macrodef = false);
    void     ParseCommandSequence();
    void     EmitNodes(const NodeList& nodes);
    void     ParseAndAppend();
    void     ParseSequence();
};

String StringiseType(const Node& token);

} // namespace TeX

#endif // XPP_PARSER_H
