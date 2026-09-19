/* Escreve os sons do app em .wav, para escutar no PC.

   Usa gh_sintetizar() -- exatamente a mesma funcao que o console chama. Se
   fossem dois codigos, o que a gente escuta aqui nao provaria nada sobre o
   que o 3DS toca.

       cc -o /tmp/ouvir homebrew/guild-hunter/ouvir.c \
             homebrew/guild-hunter/source/nucleo.c -lm
       /tmp/ouvir <pasta>
*/
#include <stdio.h>
#include <string.h>
#include "source/nucleo.h"

static void p32(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f);
                                       fputc(v >> 16 & 255, f); fputc(v >> 24 & 255, f); }
static void p16(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }

static void gravar(const char *caminho, const short *a, size_t n, unsigned taxa)
{
    FILE *f = fopen(caminho, "wb");
    if (!f) { perror(caminho); return; }
    unsigned dados = (unsigned)(n * 2);
    fwrite("RIFF", 1, 4, f); p32(f, 36 + dados); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); p32(f, 16); p16(f, 1); p16(f, 1);
    p32(f, taxa); p32(f, taxa * 2); p16(f, 2); p16(f, 16);
    fwrite("data", 1, 4, f); p32(f, dados);
    fwrite(a, 2, n, f);
    fclose(f);
    printf("  %s  %zu amostras\n", caminho, n);
}

int main(int argc, char **argv)
{
    const char *pasta = argc > 1 ? argv[1] : ".";
    static short a[64000], tudo[400000];
    size_t total = 0;

    for (int s = 0; s < GH_SOM_TOTAL; s++) {
        size_t n = gh_sintetizar(s, a, sizeof a / sizeof *a, GH_TAXA_SOM);
        if (!n) continue;
        char caminho[512];
        snprintf(caminho, sizeof caminho, "%s/%s.wav", pasta, gh_som_nome(s));
        gravar(caminho, a, n, GH_TAXA_SOM);

        /* uma faixa com todos, para ouvir a familia de uma vez */
        if (total + n + GH_TAXA_SOM / 3 < sizeof tudo / sizeof *tudo) {
            memcpy(tudo + total, a, n * sizeof *a);
            total += n;
            memset(tudo + total, 0, (GH_TAXA_SOM / 3) * sizeof *tudo);
            total += GH_TAXA_SOM / 3;
        }
    }
    char caminho[512];
    snprintf(caminho, sizeof caminho, "%s/todos.wav", pasta);
    gravar(caminho, tudo, total, GH_TAXA_SOM);
    return 0;
}
