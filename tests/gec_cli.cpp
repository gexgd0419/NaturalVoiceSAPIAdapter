#include "../NaturalVoiceSAPIAdapter/EdgeGec.h"
#include <stdlib.h>
int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        char token[82];
        EdgeGec::Generate(strtoull(argv[i], nullptr, 10), token);
        puts(token);
    }
}
