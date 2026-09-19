/* Mostra que som o app vai tocar em cada evento, para uma pasta de .wav.

   Responde duas perguntas sem cartão e sem console: "os meus arquivos vão
   tocar?" e "o que acontece nos eventos para os quais eu não trouxe som?".
   O app é silencioso quando um arquivo não serve -- ele empresta de outro ou
   cai na síntese -- e descobrir isso só no 3DS é caro.

       cc -I source -o /tmp/conferir conferir-sons.c source/nucleo.c -lm
       /tmp/conferir /caminho/da/pasta/sons
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "source/nucleo.h"

/* Devolve 1 se o arquivo existe e o app consegue lê-lo. */
static int serve(const char *pasta, const char *nome, double *dur)
{
    char caminho[512];
    snprintf(caminho, sizeof caminho, "%s/%s.wav", pasta, nome);

    FILE *f = fopen(caminho, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long tam = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *d = malloc(tam > 0 ? (size_t)tam : 1);
    size_t lido = d ? fread(d, 1, (size_t)tam, f) : 0;
    fclose(f);

    size_t ini = 0, n = 0;
    unsigned taxa = 0;
    int ok = gh_wav_pcm16(d, lido, &ini, &n, &taxa);
    if (ok && dur) *dur = (double)n / taxa;
    free(d);
    return ok;
}

int main(int argc, char **argv)
{
    const char *pasta = argc > 1 ? argv[1] : ".";
    unsigned char tem[GH_SOM_TOTAL];
    double dur[GH_SOM_TOTAL];
    int problema = 0;

    for (int i = 0; i < GH_SOM_TOTAL; i++) {
        dur[i] = 0;
        tem[i] = serve(pasta, gh_som_nome(i), &dur[i]) ? 1 : 0;
    }

    printf("sons em %s\n\n", pasta);
    for (int i = 0; i < GH_SOM_TOTAL; i++) {
        printf("  %-20s ", gh_som_nome(i));
        if (tem[i]) {
            printf("arquivo próprio (%.2fs)\n", dur[i]);
            continue;
        }
        int outro = gh_som_alternativa(i, tem);
        if (outro >= 0) printf("emprestado de %s\n", gh_som_nome(outro));
        else            { printf("sintetizado\n"); problema = 1; }
    }

    double d = 0;
    printf("\n  %-20s %s\n", "musica",
           serve(pasta, "musica", &d) ? "arquivo próprio" : "(sem música de fundo)");
    if (d > 0) printf("  %-20s %.0f s em loop\n", "", d);

    if (problema)
        printf("\n  Nada errado -- só quer dizer que ali toca o som calculado\n"
               "  do app, que não combina com gravações de jogo.\n");
    return 0;
}
