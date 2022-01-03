#include "parser.h"

#include <clocale>
#include <iostream>
#include <utils/utils.h>

int main() {
    setlocale(LC_ALL, "");
    TeX::Parser p{"test.tex"};
    p.PrintAllTokens();
}
