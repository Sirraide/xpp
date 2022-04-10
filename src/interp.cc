/*
#include "parser.h"

namespace TeX {

std::map<String, std::function<void(Parser& p)>> builtin_functions{
    {U"\\Inject", [](Parser& p) {
        /// TODO: implement
     }},
};

struct InterpContext {
    Parser&                                          p;
    std::map<String, std::function<void(Parser& p)>> functions;

    InterpContext(Parser& _p) : p(_p) {}

    void Eval() {
        p.NextNonWhitespaceToken(); /// Yeet \Eval
        if (p.at_eof) Die("Missing argument for \\Eval");
        if (p.token.type != TokenType::CommandSequence) Die("Expected command sequence at top level in \\Eval");

        /// Call the function
        if (functions.contains(p.token.string_content)) functions.at(p.token.string_content)(p);
        else if (builtin_functions.contains(p.token.string_content)) builtin_functions.at(p.token.string_content)(p);
        else Die("Unknown command sequence '%s' in \\Eval", ToUTF8(p.token.string_content).c_str());
    }
};

void Parser::HandleEval() {
    InterpContext{*this}.Eval();
}

} // namespace TeX*/
