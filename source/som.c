/* Som do Guild Hunter.

   Os sons sao calculados, nao gravados: o audio do Monster Hunter e da Capcom
   e nao entra num repositorio publico. A familia sonora e a do balcao da
   guilda -- madeira seca, nada eletronico -- e sai de gh_sintetizar(), que
   mora no nucleo justamente para poder ser conferida no PC.

   Quem tem o jogo pode usar as proprias gravacoes: se existir
   sdmc:/3ds/guild-hunter/sons/<nome>.wav (PCM 16 bits, mono), ele vale mais
   que a sintese. Fica na maquina de quem gravou; nada disso e distribuido.

   Se o DSP nao subir, o app roda calado em vez de morrer. Som e enfeite;
   cadastrar nao e.

   Sobre isso: a libctru CARREGA a firmware do DSP antes de usa-lo, lendo
   sdmc:/3ds/dspfirm.cdc, e desiste se nao achar. Jogo de varejo nao passa por
   esse caminho -- por isso um jogo toca som no emulador e o homebrew nao, o
   que parece defeito nosso e nao e. Num console com CFW, o DSP1 gera o
   arquivo uma vez e resolve para sempre.
*/

#include <3ds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nucleo.h"
#include "som.h"

/* Tres fontes, nesta ordem:

     1. sdmc:/3ds/guild-hunter/sons/   -- trocavel sem recompilar nada
     2. romfs:/sons/                   -- embutido no .3dsx no momento do build
     3. sintese                        -- sempre existe

   O embutido resolve uma coisa concreta: quem monta o proprio build pode
   levar o audio dentro do arquivo, sem depender de copiar pasta nenhuma para
   o cartao. E o audio embutido e o de quem montou -- este repositorio nao
   carrega som de terceiro. */
#define PASTA_SD    "sdmc:/3ds/guild-hunter/sons"
#define PASTA_ROMFS "romfs:/sons"
#define ARQ_MUSICA_SD    PASTA_SD    "/musica.wav"
#define ARQ_MUSICA_ROMFS PASTA_ROMFS "/musica.wav"

/* O ndsp tem 24 canais (0..23). Os efeitos ocupam um cada, pelo id; a musica
   vai no ultimo, longe deles. Com 5 efeitos o canal 7 estava livre -- com 9,
   o som de "error" cairia em cima da musica e a cortaria a cada recusa. */
#define CANAL_MUSICA 23
/* Um quarto de segundo por bloco: curto o bastante para a troca ser rapida,
   longo o bastante para o disco nao ser cobrado a todo quadro. */
#define BLOCO_MUSICA 8000
#define N_BLOCOS     4
#define LIMITE_ARQUIVO (512 * 1024)     /* cabe ~8 s a 32 kHz; de sobra */
#define MAX_SINTESE    (GH_TAXA_SOM)    /* 1 s: mais que qualquer som nosso */

typedef struct {
    short       *amostras;
    size_t       n;
    unsigned     taxa;
    ndspWaveBuf  buf;
    int          fonte;     /* de quem sai o audio: ele mesmo, ou um emprestado */
} Som;

static Som sons[GH_SOM_TOTAL];
static int ligado, trocados;

int som_disponivel(void)   { return ligado; }
int som_substituidos(void) { return trocados; }

/* Le um .wav de onde estiver. O que decide se o arquivo serve e
   gh_wav_pcm16(), no nucleo -- aqui e so disco e memoria. */
static int carregar_wav(const char *caminho, Som *s)
{
    FILE *f = fopen(caminho, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long tam = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (tam <= 0 || tam > LIMITE_ARQUIVO) { fclose(f); return 0; }

    unsigned char *bruto = malloc((size_t)tam);
    if (!bruto) { fclose(f); return 0; }
    size_t lido = fread(bruto, 1, (size_t)tam, f);
    fclose(f);

    size_t inicio = 0, n = 0;
    unsigned taxa = 0;
    if (!gh_wav_pcm16(bruto, lido, &inicio, &n, &taxa)) { free(bruto); return 0; }

    s->amostras = linearAlloc(n * sizeof(short));
    if (!s->amostras) { free(bruto); return 0; }
    memcpy(s->amostras, bruto + inicio, n * sizeof(short));
    free(bruto);

    s->n = n;
    s->taxa = taxa;
    return 1;
}

static int carregar_arquivo(int id, Som *s)
{
    const char *nome = gh_som_nome(id);
    char caminho[128];
    if (!nome) return 0;

    snprintf(caminho, sizeof caminho, "%s/%s.wav", PASTA_SD, nome);
    if (carregar_wav(caminho, s)) return 1;

    snprintf(caminho, sizeof caminho, "%s/%s.wav", PASTA_ROMFS, nome);
    return carregar_wav(caminho, s);
}

static int sintetizar_para(int id, Som *s)
{
    s->amostras = linearAlloc(MAX_SINTESE * sizeof(short));
    if (!s->amostras) return 0;
    s->n = gh_sintetizar(id, s->amostras, MAX_SINTESE, GH_TAXA_SOM);
    s->taxa = GH_TAXA_SOM;
    if (s->n == 0) { linearFree(s->amostras); s->amostras = NULL; return 0; }
    return 1;
}

/* ------------------------------------------------------------- musica --

   A trilha é lida em ADPCM, no formato GHA1 que `gerar_musica.c` escreve.

   PCM16 pesava 4x mais -- a música sozinha eram 10,3 MB dos 10,7 do .cia --
   e o peso não era só espaço: cada bloco custava 16 KB de leitura de cartão
   no meio do laço, e é isso que engasga. O DSP toca ADPCM direto, sem a CPU
   decodificar, então arquivo e leitura encolhem juntos: 8 KB por bloco.

   Cada bloco precisa do CONTEXTO (as duas últimas amostras decodificadas do
   bloco anterior). Quem decodifica é o DSP, então o app não teria como saber
   -- por isso o arquivo traz uma tabela pronta, 6 bytes por bloco.
*/

#define QUADROS_BLOCO 1024
#define BYTES_BLOCO   (QUADROS_BLOCO * 8)
#define AMOSTRAS_BLOCO (QUADROS_BLOCO * GH_ADPCM_QUADRO)

static FILE         *mus;
static unsigned      mus_taxa;
static unsigned      mus_blocos;        /* quantos blocos o arquivo tem */
static size_t        mus_dados;         /* onde começam os bytes ADPCM */
static short         mus_filtros[GH_ADPCM_FILTROS * 2];
static short        *mus_ctx;           /* 3 shorts por bloco */
static unsigned      mus_prox;          /* próximo bloco a ler */

static unsigned char *mus_bloco[N_BLOCOS];
static ndspWaveBuf    mus_wb[N_BLOCOS];
static ndspAdpcmData  mus_ad[N_BLOCOS];
static unsigned       mus_bloco_de[N_BLOCOS];   /* qual bloco cada buffer tem */

static int mus_existe, mus_tocando;
static volatile int religar_em;   /* quadros até refazer a música depois do HOME */

/* A música reabastece numa THREAD, não no laço de desenho: ler 8 KB do cartão
   no meio do frame era o que engasgava o áudio. `mus_trava` serializa o estado
   da música entre a thread e o laço. O laço de desenho NÃO pega a trava em
   quadro normal (ver musica_passo) -- se pegasse, esperaria o fread da thread
   e o engasgo voltaria pela porta dos fundos. */
static LightLock    mus_trava;
static Thread       mus_thread;
static volatile int mus_thread_viva;    /* a thread foi criada e está rodando */
static volatile int mus_parar_thread;   /* sinal de saída para a thread */

int musica_existe(void)  { return mus_existe; }
int musica_tocando(void) { return mus_tocando; }

static FILE *abrir_musica(void)
{
    FILE *f = fopen(PASTA_SD "/musica.gha", "rb");
    return f ? f : fopen(PASTA_ROMFS "/musica.gha", "rb");
}

static void musica_preparar(void)
{
    unsigned char cab[52];

    mus = abrir_musica();
    if (!mus) return;
    if (fread(cab, 1, sizeof cab, mus) != sizeof cab
        || memcmp(cab, "GHA1", 4) != 0) {
        fclose(mus); mus = NULL; return;
    }

    memcpy(&mus_taxa, cab + 4, 4);
    unsigned total, por_bloco;
    memcpy(&total, cab + 8, 4);
    memcpy(&por_bloco, cab + 12, 4);
    memcpy(&mus_blocos, cab + 16, 4);
    memcpy(mus_filtros, cab + 20, sizeof mus_filtros);

    /* O arquivo tem de combinar com o que este código espera; se alguém gerar
       com outro tamanho de bloco, é melhor ficar sem música do que tocar
       lixo. */
    if (por_bloco != AMOSTRAS_BLOCO || mus_blocos == 0
        || mus_taxa < 8000 || mus_taxa > 48000) {
        fclose(mus); mus = NULL; return;
    }

    mus_ctx = malloc((size_t)mus_blocos * 3 * sizeof(short));
    if (!mus_ctx || fread(mus_ctx, sizeof(short), mus_blocos * 3, mus)
                    != (size_t)mus_blocos * 3) {
        free(mus_ctx); mus_ctx = NULL;
        fclose(mus); mus = NULL; return;
    }
    mus_dados = sizeof cab + (size_t)mus_blocos * 3 * sizeof(short);

    for (int i = 0; i < N_BLOCOS; i++) {
        mus_bloco[i] = linearAlloc(BYTES_BLOCO);
        if (!mus_bloco[i]) {
            for (int j = 0; j < i; j++) linearFree(mus_bloco[j]);
            free(mus_ctx); mus_ctx = NULL;
            fclose(mus); mus = NULL; return;
        }
    }
    mus_existe = 1;
}

/* Lê o bloco `n` para o buffer `i`, com o contexto dele. */
static void carregar_bloco(int i, unsigned n)
{
    if (n >= mus_blocos) n = 0;
    fseek(mus, (long)(mus_dados + (size_t)n * BYTES_BLOCO), SEEK_SET);
    size_t lidos = fread(mus_bloco[i], 1, BYTES_BLOCO, mus);
    if (lidos < BYTES_BLOCO)
        memset(mus_bloco[i] + lidos, 0, BYTES_BLOCO - lidos);

    mus_ad[i].index    = (u16)mus_ctx[n * 3];
    mus_ad[i].history0 = mus_ctx[n * 3 + 1];
    mus_ad[i].history1 = mus_ctx[n * 3 + 2];
    mus_bloco_de[i] = n;

    DSP_FlushDataCache(mus_bloco[i], BYTES_BLOCO);
    DSP_FlushDataCache(&mus_ad[i], sizeof mus_ad[i]);
}

static void enfileirar(int i)
{
    memset(&mus_wb[i], 0, sizeof mus_wb[i]);
    mus_wb[i].data_vaddr = mus_bloco[i];
    mus_wb[i].nsamples   = AMOSTRAS_BLOCO;
    mus_wb[i].adpcm_data = &mus_ad[i];
    ndspChnWaveBufAdd(CANAL_MUSICA, &mus_wb[i]);
}

static void musica_comecar(int do_inicio)
{
    if (!mus_existe || mus_tocando) return;
    if (do_inicio) mus_prox = 0;

    /* Limpar a fila ANTES de reconfigurar: re-adicionar um waveBuf que ainda
       está na fila encadeia a lista nela mesma, e aí o áudio degrada, some e
       o console trava. Foi exatamente o que o Marcos viu apertando SELECT
       várias vezes. */
    ndspChnWaveBufClear(CANAL_MUSICA);
    ndspChnReset(CANAL_MUSICA);
    ndspChnSetInterp(CANAL_MUSICA, NDSP_INTERP_LINEAR);
    ndspChnSetRate(CANAL_MUSICA, (float)mus_taxa);
    ndspChnSetFormat(CANAL_MUSICA, NDSP_FORMAT_MONO_ADPCM);
    ndspChnSetAdpcmCoefs(CANAL_MUSICA, (u16 *)mus_filtros);

    float mix[12];
    memset(mix, 0, sizeof mix);
    mix[0] = mix[1] = 0.40f;      /* é fundo, não é o assunto */
    ndspChnSetMix(CANAL_MUSICA, mix);

    for (int i = 0; i < N_BLOCOS; i++) {
        carregar_bloco(i, mus_prox);
        mus_prox = (mus_prox + 1) % mus_blocos;
        enfileirar(i);
    }
    mus_tocando = 1;
}

static void musica_parar(void)
{
    if (!mus_tocando) return;

    /* Retoma de onde a pessoa ouviu: o bloco que ainda estava tocando. Pausa
       que volta do começo não é pausa. */
    u16 seq = ndspChnGetWaveBufSeq(CANAL_MUSICA);
    for (int i = 0; i < N_BLOCOS; i++)
        if (mus_wb[i].status != NDSP_WBUF_DONE && mus_wb[i].sequence_id == seq) {
            mus_prox = mus_bloco_de[i];
            break;
        }

    ndspChnWaveBufClear(CANAL_MUSICA);
    ndspChnReset(CANAL_MUSICA);
    mus_tocando = 0;
}

void musica_alternar(void)
{
    if (!ligado || !mus_existe) return;
    LightLock_Lock(&mus_trava);
    if (mus_tocando) musica_parar();
    else             musica_comecar(0);
    LightLock_Unlock(&mus_trava);
}

static void reabastecer(void)
{
    if (!ligado || !mus_tocando || religar_em > 0) return;
    for (int i = 0; i < N_BLOCOS; i++) {
        if (mus_wb[i].status == NDSP_WBUF_DONE) {
            carregar_bloco(i, mus_prox);
            mus_prox = (mus_prox + 1) % mus_blocos;
            enfileirar(i);
        }
    }
}

static void refazer_depois_do_home(void)
{
    for (int i = 0; i < GH_SOM_TOTAL; i++)
        if (sons[i].amostras)
            DSP_FlushDataCache(sons[i].amostras, sons[i].n * sizeof(short));

    if (!mus_existe || !mus_tocando) return;
    /* O sistema tomou o DSP; recua os blocos que estavam na fila. */
    unsigned recuo = N_BLOCOS;
    mus_prox = (mus_prox + mus_blocos - recuo) % mus_blocos;
    mus_tocando = 0;
    musica_comecar(0);
}

/* A thread do reabastecimento. Sai limpa: o laço é curto e checa
   `mus_parar_thread` a cada volta, então o threadJoin do som_encerrar retorna
   em ~10 ms -- foi o join preso que derrubou a primeira versão (36fad3d).
   Segura `mus_trava` só durante o reabastecer. */
static void mus_thread_fn(void *arg)
{
    (void)arg;
    while (!mus_parar_thread) {
        LightLock_Lock(&mus_trava);
        reabastecer();
        LightLock_Unlock(&mus_trava);
        svcSleepThread(10 * 1000 * 1000LL);   /* 10 ms << 32 ms de um bloco */
    }
}

void musica_passo(void)
{
    /* Roda no laço de desenho, a cada quadro. Com a thread viva e em jogo
       normal, NÃO toca a trava: quem reabastece é a thread, fora do laço. Só o
       religar do HOME (uma vez) e o modo reserva (sem thread) pegam a trava. */
    if (!ligado) return;
    if (religar_em > 0) {
        if (--religar_em == 0) {
            LightLock_Lock(&mus_trava);
            refazer_depois_do_home();
            LightLock_Unlock(&mus_trava);
        }
        return;
    }
    if (!mus_thread_viva) {              /* reserva: sem thread, reabastece aqui */
        LightLock_Lock(&mus_trava);
        reabastecer();
        LightLock_Unlock(&mus_trava);
    }
}

void som_iniciar(int sem_musica)
{
    ligado = 0;
    trocados = 0;
    religar_em = 0;
    mus_thread_viva = 0;
    mus_parar_thread = 0;
    LightLock_Init(&mus_trava);
    memset(sons, 0, sizeof sons);

    if (R_FAILED(ndspInit())) return;      /* sem DSP: segue calado */
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);

    unsigned char tem[GH_SOM_TOTAL];
    for (int i = 0; i < GH_SOM_TOTAL; i++) {
        sons[i].fonte = i;
        tem[i] = carregar_arquivo(i, &sons[i]) ? 1 : 0;
        if (tem[i]) trocados++;
    }

    /* Quem trouxe os proprios sons quase nunca traz os nove. O que faltar pega
       emprestado do conjunto em vez de cair na sintese -- um som calculado no
       meio de gravacoes de jogo soa como defeito, e foi assim que um "tuk"
       sintetico apareceu no lugar do som certo. */
    for (int i = 0; i < GH_SOM_TOTAL; i++) {
        if (tem[i]) continue;
        int emprestado = gh_som_alternativa(i, tem);
        if (emprestado >= 0) {
            sons[i].fonte = emprestado;      /* nao ocupa memoria nenhuma */
            continue;
        }
        if (!sintetizar_para(i, &sons[i])) continue;
    }

    for (int i = 0; i < GH_SOM_TOTAL; i++)
        if (sons[i].amostras)
            DSP_FlushDataCache(sons[i].amostras, sons[i].n * sizeof(short));

    ligado = 1;

    if (!sem_musica) {
        musica_preparar();
        musica_comecar(1);      /* primeira vez: do começo */
        if (mus_existe) {
            /* A thread na mesma prioridade do laço: acorda a cada 10 ms,
               reabastece e volta a dormir -- roda na espera do vblank, sem
               disputar o desenho. Se não subir (thread == NULL), musica_passo
               cai no modo reserva e o áudio ainda toca. */
            s32 prio = 0x30;
            svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
            mus_parar_thread = 0;
            mus_thread = threadCreate(mus_thread_fn, NULL, 16 * 1024,
                                      prio, -1, false);
            mus_thread_viva = (mus_thread != NULL);
        }
    }

}

/* Chamado pelo laço principal quando o gancho do APT avisa. Não faz trabalho:
   só marca. Quem trabalha é a thread da música, meio segundo depois. */
void som_retomar(void)
{
    if (!ligado) return;
    religar_em = 30;   /* ~meio segundo a 60 quadros */
}

void som_tocar(int id)
{
    if (!ligado || id < 0 || id >= GH_SOM_TOTAL) return;

    /* O audio pode vir de outro som (emprestado), mas o canal e o waveBuf sao
       deste id: assim dois eventos que compartilham a mesma gravacao nao se
       atropelam ao tocar juntos. */
    Som *s = &sons[sons[id].fonte];
    if (!s->amostras || s->n == 0) return;

    /* Um canal por som: dois avisos seguidos nao se atropelam, e retocar o
       mesmo som corta o anterior em vez de enfileirar. */
    int canal = id;
    /* Limpar a fila ANTES de reconfigurar. Sem isto, tocar o mesmo som de
       novo enquanto o anterior ainda está na fila re-encadeia o mesmo
       waveBuf na lista do ndsp -- e uma lista que aponta para si mesma
       degrada o áudio e acaba travando o console. */
    ndspChnWaveBufClear(canal);
    ndspChnReset(canal);
    ndspChnSetInterp(canal, NDSP_INTERP_LINEAR);
    ndspChnSetRate(canal, (float)s->taxa);
    ndspChnSetFormat(canal, NDSP_FORMAT_MONO_PCM16);

    float mix[12];
    memset(mix, 0, sizeof mix);
    mix[0] = mix[1] = 0.85f;
    ndspChnSetMix(canal, mix);

    ndspWaveBuf *wb = &sons[id].buf;
    memset(wb, 0, sizeof *wb);
    wb->data_vaddr = s->amostras;
    wb->nsamples   = (u32)s->n;
    ndspChnWaveBufAdd(canal, wb);
}

void som_encerrar(void)
{
    if (!ligado) return;

    /* Recolher a thread ANTES de fechar o arquivo e soltar os buffers que ela
       usa. O join volta rápido (a thread checa a flag a cada 10 ms); o timeout
       é só rede de segurança -- é o que faltava em 36fad3d, quando o
       fechamento ficava preso aqui para sempre. */
    if (mus_thread_viva) {
        mus_parar_thread = 1;
        threadJoin(mus_thread, 2000000000ULL);   /* 2 s de folga */
        threadFree(mus_thread);
        mus_thread_viva = 0;
    }

    musica_parar();
    if (mus) { fclose(mus); mus = NULL; }
    for (int i = 0; i < N_BLOCOS; i++)
        if (mus_bloco[i]) { linearFree(mus_bloco[i]); mus_bloco[i] = NULL; }
    free(mus_ctx); mus_ctx = NULL;
    mus_existe = 0;
    for (int i = 0; i < GH_SOM_TOTAL; i++) {
        ndspChnReset(i);
        if (sons[i].amostras) linearFree(sons[i].amostras);
    }
    ndspExit();
    ligado = 0;
}
