#include "parser.h"

#include <clocale>

int main(int argc, char** argv) {
    setlocale(LC_ALL, "");
    TeX::Parser p{TeX::Parser::options::parse(argc, argv)};
}
