#ifndef XPP_NATIVE_PARSER_H
#define XPP_NATIVE_PARSER_H

#include <string>
#include <utils/parser.h>
#include <utils/utils.h>

namespace TeX {
/*struct AST {
};*/

enum struct TokenType : Char {
    Invalid,
    Text,
    EndOfFile = Eof,
    CommandSequence = U'\\',
    LineComment = U'%',
    GroupBegin = U'{',
    GroupEnd   = U'}',
};

using AbstractLexer = LexerBase<FileBase, SourceLocationBase<>, TokenBase<TokenType>, false>;
struct Parser : public AbstractLexer {
    explicit Parser(const std::string& filename);

    void LexLineComment();
    void LexCommandSequence();
    void LexText();
    void NextToken() override;
    // static AST Parse(std::string filename);
};

String StringiseType(const Parser::Token* token);

} // namespace TeX

#endif // XPP_NATIVE_PARSER_H
