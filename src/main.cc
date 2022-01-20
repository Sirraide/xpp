#include "parser.h"

#include <clocale>

Clopts opts{
    {"-o", "The file to output to"},
    {"filename", "The file to be processed", CT::String, true, true},
    {"--print-tokens", "Print all tokens to stdout and exit", CT::Void},
};

int main(int argc, char** argv) {
    setlocale(LC_ALL, "");
    opts.Parse(argc, argv);
    TeX::Parser p{opts};
}
