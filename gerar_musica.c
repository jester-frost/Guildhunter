/* Converte a trilha em ADPCM para o app tocar por streaming.

   PCM16 pesava 4x mais: a música sozinha eram 10,3 MB dos 10,7 do .cia. E o
   peso não é só espaço -- é leitura de cartão. Cada bloco custava 16 KB de
   leitura no meio do laço, e é isso que engasga a música.

   O DSP do 3DS toca ADPCM direto, sem a CPU decodificar. O arquivo encolhe 4x
   E a leitura encolhe 4x, pelo mesmo motivo.

   A tabela de contexto existe porque em ADPCM cada amostra depende das duas
   anteriores, e quem decodifica é o DSP -- o app não sabe quais foram as duas
   últimas amostras de um bloco. Então calculamos aqui, uma vez, e anotamos o
   contexto no início de cada bloco. Custa 6 bytes por bloco.

       cc -I source -o /tmp/gerar gerar_musica.c source/nucleo.c -lm
       /tmp/gerar musica.wav musica.gha
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>

#include "source/nucleo.h"

#define QUADROS_POR_BLOCO 1024                       /* 8 KB por leitura */
#define AMOSTRAS_BLOCO (QUADROS_POR_BLOCO * GH_ADPCM_QUADRO)

static void p32(FILE *f, unsigned v) { fwrite(&v, 4, 1, f); }

int main(int argc, char **argv)
{
    if (argc < 3) { puts("uso: gerar_musica entrada.wav saida.gha"); return 2; }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long tam = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *bruto = malloc(tam);
    size_t lido = fread(bruto, 1, tam, f);
    fclose(f);

    size_t inicio, n;
    unsigned taxa;
    if (!gh_wav_pcm16(bruto, lido, &inicio, &n, &taxa)) {
        fprintf(stderr, "precisa ser WAV PCM 16 bits mono\n");
        return 1;
    }
    const short *a = (const short *)(bruto + inicio);
    printf("  entrada: %zu amostras a %u Hz  (%.1f s)\n", n, taxa, (double)n / taxa);

    short filtros[GH_ADPCM_FILTROS * 2];
    gh_adpcm_filtros(a, n, filtros);

    size_t cap = ((n + GH_ADPCM_QUADRO - 1) / GH_ADPCM_QUADRO) * 8 + 8;
    unsigned char *dados = malloc(cap);
    size_t bytes = gh_adpcm_codificar(a, n, filtros, dados);

    /* contexto no começo de cada bloco: roda o decodificador e anota */
    size_t bytes_bloco = QUADROS_POR_BLOCO * 8;
    size_t n_blocos = (bytes + bytes_bloco - 1) / bytes_bloco;
    short *ctx = calloc(n_blocos * 3, sizeof(short));
    short *tmp = malloc(AMOSTRAS_BLOCO * sizeof(short));
    short ant1 = 0, ant2 = 0;
    for (size_t b = 0; b < n_blocos; b++) {
        size_t o = b * bytes_bloco;
        size_t quanto = (bytes - o < bytes_bloco) ? bytes - o : bytes_bloco;
        ctx[b * 3]     = (short)(unsigned char)dados[o];   /* ps do 1o quadro */
        ctx[b * 3 + 1] = ant1;
        ctx[b * 3 + 2] = ant2;
        gh_adpcm_decodificar(dados + o, quanto, filtros, tmp,
                             AMOSTRAS_BLOCO, &ant1, &ant2);
    }

    /* qualidade: conferir é obrigatório num codec -- errado não dá erro, dá
       barulho, e quem descobre é quem liga o console */
    short *volta = malloc(n * sizeof(short));
    short z1 = 0, z2 = 0;
    size_t m = gh_adpcm_decodificar(dados, bytes, filtros, volta, n, &z1, &z2);
    double sinal = 0, ruido = 0;
    for (size_t i = 0; i < m; i++) {
        double s = a[i], d = (double)a[i] - volta[i];
        sinal += s * s; ruido += d * d;
    }
    double snr = 10.0 * log10(sinal / (ruido > 0 ? ruido : 1e-9));

    FILE *o = fopen(argv[2], "wb");
    if (!o) { perror(argv[2]); return 1; }
    fwrite("GHA1", 1, 4, o);
    p32(o, taxa);
    p32(o, (unsigned)n);
    p32(o, AMOSTRAS_BLOCO);
    p32(o, (unsigned)n_blocos);
    fwrite(filtros, sizeof(short), GH_ADPCM_FILTROS * 2, o);
    fwrite(ctx, sizeof(short), n_blocos * 3, o);
    fwrite(dados, 1, bytes, o);
    long saiu = ftell(o);
    fclose(o);

    printf("  %s: %ld bytes em %zu blocos (%.0f%% do PCM), %.1f dB\n",
           argv[2], saiu, n_blocos, saiu * 100.0 / (n * 2), snr);
    return snr < 12 ? 1 : 0;
}
