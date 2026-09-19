/* Guild Hunter -- cadastro de caçador nas guildas locais.

   O que este programa faz
   -----------------------
   Um servidor local de Monster Hunter precisa da credencial NEX do console
   para deixar a pessoa entrar. Até aqui só havia dois jeitos de conseguir:
   apagar as credenciais (perde amigos e friend code) ou mandar o dump do save
   para um estranho. Este app é a terceira via -- ele lê a credencial no
   próprio console e manda para a guilda escolhida, pela rede local.

   Nada de decisão mora aqui: isto é desenho, entrada e serviços do 3DS. A
   lógica está em nucleo.c, que roda e é testada no PC.

   A senha nunca aparece na tela nem em arquivo: é lida, enviada e esquecida.
*/

#include <3ds.h>
#include <citro2d.h>
#include <3ds/util/utf.h>

#include <malloc.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "nucleo.h"
#include "som.h"

#define LARG_TOPO   400
#define LARG_BASE   320
#define ALTURA      240

#define PASTA       "sdmc:/3ds/guild-hunter"
#define ARQUIVO     PASTA "/perfis.txt"

#define SOC_ALINHA  0x1000
#define SOC_TAMANHO 0x100000

/* Paleta de caderno de guilda: pergaminho, tinta e o carmim das fichas de
   missão. Desenhada com primitivas -- nenhum recurso de terceiro aqui. */
#define COR_NOITE      C2D_Color32(0x1A, 0x16, 0x12, 0xFF)
#define COR_PERGAMINHO C2D_Color32(0xE6, 0xD9, 0xBB, 0xFF)
#define COR_PAPEL      C2D_Color32(0xD6, 0xC6, 0xA2, 0xFF)
#define COR_TINTA      C2D_Color32(0x33, 0x26, 0x1B, 0xFF)
#define COR_CARMIM     C2D_Color32(0x9B, 0x2E, 0x2E, 0xFF)
#define COR_OURO       C2D_Color32(0xC9, 0x9B, 0x38, 0xFF)
#define COR_VERDE      C2D_Color32(0x3F, 0x76, 0x45, 0xFF)
#define COR_CINZA      C2D_Color32(0x8A, 0x7E, 0x6A, 0xFF)
#define COR_CLARA      C2D_Color32(0xF2, 0xE9, 0xD4, 0xFF)

enum { BT_SONDAR, BT_CADASTRAR, BT_NOVA, BT_ROTA, N_BOTOES };

/* Duas telas. A lista e a tela inicial; o formulario entra por cima quando se
   cria ou edita uma guilda.

   O formulario existe porque a fila de teclados era cega: a pessoa nao via o
   que ja tinha preenchido, nao dava para voltar num campo, e errar a porta no
   segundo de quatro passos significava recomecar tudo. Com os campos na tela,
   erra-se e corrige-se no mesmo lugar. */
typedef enum { TELA_LISTA = 0, TELA_FORM } Tela;

enum { CAMPO_NOME, CAMPO_IP, CAMPO_PORTA, CAMPO_ROTA, N_CAMPOS };
enum { FORM_SALVAR = N_CAMPOS, FORM_SAIR, N_FORM_ITENS };

static const int CAMPO_ROTULO[N_CAMPOS] = { GH_T_C_NOME, GH_T_C_END,
                                            GH_T_C_PORTA, GH_T_C_ROTA };

typedef struct { float x, y, l, a; } Caixa;

static const Caixa BOTOES[N_BOTOES] = {
    {   8, 168, 146, 30 },
    { 166, 168, 146, 30 },
    {   8, 202, 146, 30 },
    { 166, 202, 146, 30 },
};
static const int ROTULO[N_BOTOES] = { GH_T_BT_SONDAR, GH_T_BT_CADASTRAR,
                                      GH_T_BT_NOVA, GH_T_BT_EDITAR };

#define LINHAS_VISIVEIS 4
#define LINHA_Y0        32
#define LINHA_ALT       32

#define CAMPO_Y0        34
#define CAMPO_ALT       36

static const Caixa BOTOES_FORM[2] = {
    {   8, 190, 146, 34 },      /* Salvar   */
    { 166, 190, 146, 34 },      /* Sair     */
};

/* Diálogo de confirmação. Salvar altera a lista que a pessoa vai usar depois,
   e num formulário de quatro campos é fácil tocar em Salvar sem querer -- os
   botões ficam logo abaixo do último campo. Uma pergunta a mais custa um
   toque; desfazer custa reabrir e redigitar. */
static const Caixa CAIXA_MODAL   = {  36,  72, 248, 108 };
static const Caixa BOTOES_MODAL[2] = {
    {  52, 132,  96, 34 },      /* Sim  */
    { 172, 132,  96, 34 },      /* Não  */
};

/* ---------------------------------------------------------------- estado */

typedef struct {
    char  ip[GH_TAM_IP];
    char  dns[GH_TAM_IP];
    char  gateway[GH_TAM_IP];
    GhDns classe;
} Rede;

static C3D_RenderTarget *alvo_topo, *alvo_base;
static C2D_TextBuf       buf_texto;

static GhPerfil perfis[GH_MAX_PERFIS];
static int      qtd, foco, rolagem;

/* Quais guildas as ações vão atingir. Fica fora do GhPerfil de propósito: é
   estado de tela, não pertence ao arquivo do cartão. */
static unsigned char marcada[GH_MAX_PERFIS];

static Tela     tela;
static GhPerfil rascunho;       /* a guilda sendo montada ou editada */
static int      form_alvo;      /* indice em edicao, ou -1 para uma nova */
static int      form_foco;
/* Apagar some com a guilda e com o histórico dela -- é tão irreversível
   quanto salvar por cima, e merece a mesma pergunta. */
enum { MODAL_NENHUM = 0, MODAL_SALVAR, MODAL_APAGAR, MODAL_SAIR };
static int      modal;
static int      quer_sair;      /* o gh_texto(GH_T_SIM, idioma) do modal de saída avisa o laço */
static int      modal_foco;     /* 0 = Sim, 1 = Não */

static Rede     rede;
static u32      meu_pid;
static char     minha_senha[GH_TAM_IP * 2];   /* 16 + folga; nunca vai à tela */
static int      tem_credencial;
static char     erro_credencial[96];

/* Identidade do caçador, do próprio console. Nome é o da conta de amigos; o
   resto vem dos dados do Mii. */
/* Idioma da interface. Vem do console; um arquivo no cartão força outro,
   para quem tem console japonês e quer ler em português. */
static int      idioma = GH_EN;

static char     meu_nome[48];
static int      tem_mii;
static u8       mii_pele, mii_cabelo, mii_camisa, mii_rosto;

static char     recado[128];
static u32      cor_recado;

/* ---------------------------------------------------------------- desenho */

/* Deslocamento de profundidade. Sem isto o modal desenhava com z MENOR que o
   formulário atrás, e o formulário atravessava a caixa: o texto de trás
   aparecia por cima da pergunta. Quem desenha por cima sobe a camada. */
static float camada;

/* Enquanto o modal está aberto, a tela de trás desenha só as caixas vazias --
   sem texto e sem ícone.

   O texto porque o citro2d junta todo o texto do quadro e o desenha por
   último: a letra de trás sai por cima da pergunta qualquer que seja a
   profundidade. Os ícones porque eles também desenham acima da caixa -- os
   losangos e principalmente os riscos vermelhos, que cruzavam a pergunta.

   Calar os dois é o que resolve. Mexer em profundidade não resolveu, duas
   vezes. */
static int so_caixas;

/* Declaradas aqui porque o desenho da pergunta precisa saber qual guilda está
   escolhida, e ela é definida junto das ações, mais abaixo. */
static int  perfil_escolhido(void);
static void acao_apagar(void);
static int  alvos(int *saida);

static void escrever(float x, float y, float esc, u32 cor, const char *fmt, ...)
{
    char s[192];
    va_list ap;

    if (so_caixas) return;

    va_start(ap, fmt);
    vsnprintf(s, sizeof s, fmt, ap);
    va_end(ap);

    C2D_Text t;
    C2D_TextParse(&t, buf_texto, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f + camada, esc, esc, cor);
}

static void escrever_centro(float cx, float y, float esc, u32 cor, const char *fmt, ...)
{
    char s[192];
    va_list ap;

    if (so_caixas) return;

    va_start(ap, fmt);
    vsnprintf(s, sizeof s, fmt, ap);
    va_end(ap);

    C2D_Text t;
    C2D_TextParse(&t, buf_texto, s);
    C2D_TextOptimize(&t);
    float l = 0, a = 0;
    C2D_TextGetDimensions(&t, esc, esc, &l, &a);
    C2D_DrawText(&t, C2D_WithColor, cx - l / 2, y, 0.5f + camada, esc, esc, cor);
}

/* O losango é o motivo da casa: é dele que saem a estrela de missão, o selo
   de cadastrado e o marcador de desligado. Duas metades, sem textura. */
static void losango(float cx, float cy, float r, u32 cor)
{
    C2D_DrawTriangle(cx, cy - r, cor, cx - r, cy, cor, cx + r, cy, cor, 0.4f + camada);
    C2D_DrawTriangle(cx, cy + r, cor, cx - r, cy, cor, cx + r, cy, cor, 0.4f + camada);
}

static void losango_vazado(float cx, float cy, float r, u32 cor, float grossura)
{
    C2D_DrawLine(cx, cy - r, cor, cx + r, cy, cor, grossura, 0.45f + camada);
    C2D_DrawLine(cx + r, cy, cor, cx, cy + r, cor, grossura, 0.45f + camada);
    C2D_DrawLine(cx, cy + r, cor, cx - r, cy, cor, grossura, 0.45f + camada);
    C2D_DrawLine(cx - r, cy, cor, cx, cy - r, cor, grossura, 0.45f + camada);
}

static void visto(float cx, float cy, float r, u32 cor)
{
    C2D_DrawLine(cx - r * 0.5f, cy, cor, cx - r * 0.1f, cy + r * 0.45f, cor, 2.0f, 0.5f + camada);
    C2D_DrawLine(cx - r * 0.1f, cy + r * 0.45f, cor, cx + r * 0.55f, cy - r * 0.5f, cor, 2.0f, 0.5f + camada);
}

static void barrado(float cx, float cy, float r, u32 cor)
{
    /* na diagonal do losango, sem passar das pontas -- fora disso o traco
       escapa do icone e vira rabisco por cima do texto ao lado */
    float d = r * 0.7f;
    C2D_DrawLine(cx - d, cy - d, cor, cx + d, cy + d, cor, 1.6f, 0.6f + camada);
}

/* C2D_Color32 é função, e função não inicializa array estático. Mesmo
   empacotamento, em macro. */
#define RGB(r, g, b) ((u32)((r) | ((g) << 8) | ((b) << 16) | (0xFFu << 24)))

/* As paletas de Mii do 3DS, por índice. São APROXIMAÇÕES das cores do sistema
   -- servem para o retrato parecer com o seu Mii, não para reproduzi-lo. */
static const u32 PELE[6] = {
    RGB(0xFF, 0xE0, 0xC0), RGB(0xFF, 0xCE, 0xA0),
    RGB(0xE0, 0xA8, 0x78), RGB(0xC0, 0x80, 0x50),
    RGB(0x90, 0x50, 0x30), RGB(0x60, 0x30, 0x18),
};
static const u32 CABELO[8] = {
    RGB(0x1A, 0x1A, 0x1A), RGB(0x44, 0x22, 0x14),
    RGB(0x6B, 0x3A, 0x20), RGB(0x8C, 0x5A, 0x2B),
    RGB(0x9A, 0x9A, 0x9A), RGB(0x70, 0x50, 0x30),
    RGB(0xC8, 0xA0, 0x50), RGB(0xE8, 0xD8, 0xB0),
};
static const u32 CAMISA[12] = {
    RGB(0xD2, 0x1E, 0x14), RGB(0xFF, 0x6E, 0x19),
    RGB(0xFF, 0xD2, 0x00), RGB(0x78, 0xD2, 0x20),
    RGB(0x00, 0x7A, 0x00), RGB(0x0F, 0x2F, 0xAD),
    RGB(0x35, 0xB4, 0xE4), RGB(0xFF, 0x8F, 0xAB),
    RGB(0x6D, 0x2F, 0xA0), RGB(0x4C, 0x2D, 0x0F),
    RGB(0xE6, 0xE6, 0xE6), RGB(0x1A, 0x1A, 0x1A),
};

/* Retrato do caçador.

   Isto NÃO é o seu Mii: o 3DS não expõe um renderizador, e desenhar o Mii de
   verdade exigiria as texturas das peças de rosto do sistema, que não podemos
   embutir. É um retrato desenhado com as cores REAIS do seu Mii -- pele,
   cabelo e cor favorita -- na moldura de ficha de guilda. */
static void retrato(float cx, float cy, float r)
{
    if (so_caixas) return;
    u32 anel = tem_mii ? CAMISA[mii_camisa % 12] : COR_CINZA;
    u32 pele = tem_mii ? PELE[mii_pele % 6]      : COR_CINZA;
    u32 cab  = tem_mii ? CABELO[mii_cabelo % 8]  : COR_TINTA;

    C2D_DrawCircleSolid(cx, cy, 0.30f + camada, r, anel);
    C2D_DrawCircleSolid(cx, cy, 0.32f + camada, r - 3.0f, COR_NOITE);

    if (!tem_mii) {
        escrever_centro(cx, cy - 9, 0.5f, COR_CINZA, "?");
        return;
    }

    /* rosto: mais largo ou mais estreito conforme a forma do Mii */
    float largura = (r - 7.0f) * (0.92f + (mii_rosto % 4) * 0.04f);
    C2D_DrawEllipseSolid(cx - largura, cy - (r - 6.0f), 0.34f + camada,
                         largura * 2, (r - 6.0f) * 1.9f, pele);
    /* cabelo: uma calota por cima */
    C2D_DrawEllipseSolid(cx - largura, cy - (r - 6.0f), 0.36f + camada,
                         largura * 2, (r - 6.0f) * 0.85f, cab);
    /* olhos */
    C2D_DrawCircleSolid(cx - largura * 0.42f, cy + 1, 0.38f + camada, 1.8f, COR_TINTA);
    C2D_DrawCircleSolid(cx + largura * 0.42f, cy + 1, 0.38f + camada, 1.8f, COR_TINTA);
}

/* Caixa de seleção. Quadrada, para não se confundir com o losango de estado
   -- são duas informações diferentes na mesma linha: "vou agir nesta" e "esta
   está em que pé". */
static void caixa_selecao(float x, float y, int marcada_, int foco_)
{
    if (so_caixas) return;
    float l = 14;
    u32 borda = foco_ ? COR_CARMIM : COR_CINZA;
    C2D_DrawRectSolid(x, y, 0.35f + camada, l, l,
                      marcada_ ? COR_CARMIM : COR_PERGAMINHO);
    C2D_DrawLine(x, y, borda, x + l, y, borda, 1.5f, 0.4f + camada);
    C2D_DrawLine(x + l, y, borda, x + l, y + l, borda, 1.5f, 0.4f + camada);
    C2D_DrawLine(x + l, y + l, borda, x, y + l, borda, 1.5f, 0.4f + camada);
    C2D_DrawLine(x, y + l, borda, x, y, borda, 1.5f, 0.4f + camada);
    if (marcada_) visto(x + l / 2, y + l / 2 - 1, 7.0f, COR_CLARA);
}

/* Nota musical. Desenhada com primitivas em vez de um caractere: nem toda
   fonte do sistema tem o símbolo, e um quadrado no lugar da nota seria pior
   que texto nenhum. */
static void icone_musica(float cx, float cy, u32 cor, int mudo)
{
    if (so_caixas) return;
    /* Duas colcheias unidas pela trave. A nota solta com bandeirinha vira um
       borrão neste tamanho; o par com a trave horizontal se reconhece de
       longe, porque a forma que identifica "música" é a trave. */
    float z = 0.4f + camada;
    C2D_DrawEllipseSolid(cx - 10, cy + 2, z, 8, 6, cor);      /* cabeça esq. */
    C2D_DrawEllipseSolid(cx + 1, cy + 0, z, 8, 6, cor);       /* cabeça dir. */
    C2D_DrawRectSolid(cx - 3, cy - 11, z, 2, 15, cor);        /* haste esq.  */
    C2D_DrawRectSolid(cx + 8, cy - 13, z, 2, 15, cor);        /* haste dir.  */
    /* a trave desce um pouco à esquerda, como na pauta */
    C2D_DrawTriangle(cx - 3, cy - 11, cor, cx + 10, cy - 13, cor,
                     cx + 10, cy - 9, cor, z);
    C2D_DrawTriangle(cx - 3, cy - 11, cor, cx + 10, cy - 9, cor,
                     cx - 3, cy - 7, cor, z);
    if (mudo)
        C2D_DrawLine(cx - 13, cy + 8, COR_CARMIM, cx + 13, cy - 14, COR_CARMIM,
                     2.2f, 0.5f + camada);
}

/* O selo da guilda: dois losangos cruzados, como as fichas do balcão. */
static void emblema(float cx, float cy, float r)
{
    C2D_DrawCircleSolid(cx, cy, 0.3f, r * 1.15f, COR_CARMIM);
    losango(cx, cy, r * 0.85f, COR_OURO);
    losango_vazado(cx, cy, r * 0.5f, COR_CARMIM, 2.0f);
}

/* O ícone de cada guilda da lista. `atual` marca a que o console está usando
   agora; `ligado` cai quando não há DNS personalizado -- aí a lista inteira
   aparece barrada, porque nenhuma delas está ao alcance. */
static void icone_perfil(float cx, float cy, int estado, int atual, int ligado)
{
    if (so_caixas) return;
    u32 cor = COR_CINZA;
    if (ligado) {
        if (estado == GH_PERFIL_CADASTRADO)   cor = COR_VERDE;
        else if (estado == GH_PERFIL_SONDADO) cor = COR_OURO;
        else if (estado == GH_PERFIL_FALHOU)  cor = COR_CARMIM;
    }

    if (atual)
        C2D_DrawCircleSolid(cx, cy, 0.25f, 11.0f,
                            ligado ? COR_OURO : COR_CINZA);

    if (estado == GH_PERFIL_NOVO)
        losango_vazado(cx, cy, 8.0f, cor, 2.0f);
    else
        losango(cx, cy, 8.0f, cor);

    if (estado == GH_PERFIL_CADASTRADO) visto(cx, cy, 8.0f, COR_CLARA);
    if (estado == GH_PERFIL_FALHOU)     barrado(cx, cy, 5.0f, COR_CLARA);
    if (!ligado)                        barrado(cx, cy, 8.0f, COR_CARMIM);
}

static void moldura(Caixa c, u32 fundo, u32 borda, int grossa)
{
    C2D_DrawRectSolid(c.x, c.y, 0.1f + camada, c.l, c.a, fundo);
    float g = grossa ? 2.5f : 1.0f;
    C2D_DrawLine(c.x, c.y, borda, c.x + c.l, c.y, borda, g, 0.2f + camada);
    C2D_DrawLine(c.x + c.l, c.y, borda, c.x + c.l, c.y + c.a, borda, g, 0.2f + camada);
    C2D_DrawLine(c.x + c.l, c.y + c.a, borda, c.x, c.y + c.a, borda, g, 0.2f + camada);
    C2D_DrawLine(c.x, c.y + c.a, borda, c.x, c.y, borda, g, 0.2f + camada);
}

/* ----------------------------------------------------------------- rede  */

static void ip_texto(u32 rede_be, char *saida, size_t n)
{
    const unsigned char *b = (const unsigned char *)&rede_be;
    snprintf(saida, n, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

static void escolher_idioma(void)
{
    /* Preferência explícita ganha: sdmc:/3ds/guild-hunter/idioma com pt, en
       ou es. Sem isso, um console japonês nunca mostraria português, e é
       nesse console que este app nasceu. */
    FILE *f = fopen(PASTA "/idioma", "r");
    if (f) {
        char v[8] = {0};
        if (fgets(v, sizeof v, f)) {
            if (v[0] == 'p') { fclose(f); idioma = GH_PT; return; }
            if (v[0] == 'e' && v[1] == 's') { fclose(f); idioma = GH_ES; return; }
            if (v[0] == 'e') { fclose(f); idioma = GH_EN; return; }
        }
        fclose(f);
    }

    u8 lingua = CFG_LANGUAGE_EN;
    if (R_SUCCEEDED(cfguInit())) {
        CFGU_GetSystemLanguage(&lingua);
        cfguExit();
    }
    if (lingua == CFG_LANGUAGE_PT)      idioma = GH_PT;
    else if (lingua == CFG_LANGUAGE_ES) idioma = GH_ES;
    else                                idioma = GH_EN;   /* o resto lê inglês */
}

static void ler_rede(void)
{
    memset(&rede, 0, sizeof rede);

    SOCU_IPInfo info;
    socklen_t n = sizeof info;
    if (SOCU_GetNetworkOpt(SOL_CONFIG, NETOPT_IP_INFO, &info, &n) == 0)
        ip_texto(info.ip.s_addr, rede.ip, sizeof rede.ip);

    /* O gateway sai da rota padrão. É ele que diz se o DNS em uso veio do
       DHCP -- o 3DS não expõe a chave "DNS personalizado" em lugar nenhum. */
    static u8 rotas[1024];
    n = sizeof rotas;
    if (SOCU_GetNetworkOpt(SOL_CONFIG, NETOPT_ROUTING_TABLE, rotas, &n) == 0) {
        size_t passo = sizeof(SOCU_RoutingTableEntry);
        for (size_t o = 0; o + passo <= (size_t)n; o += passo) {
            SOCU_RoutingTableEntry e;
            memcpy(&e, rotas + o, sizeof e);
            if (e.dest_ip.s_addr == 0 && e.gateway.s_addr != 0) {
                ip_texto(e.gateway.s_addr, rede.gateway, sizeof rede.gateway);
                break;
            }
        }
    }

    static u8 tabela[336];
    n = sizeof tabela;
    if (SOCU_GetNetworkOpt(SOL_CONFIG, NETOPT_DNS_TABLE, tabela, &n) == 0) {
        size_t passo = sizeof(SOCU_DNSTableEntry);
        for (size_t o = 0; o + passo <= (size_t)n; o += passo) {
            SOCU_DNSTableEntry e;
            memcpy(&e, tabela + o, sizeof e);
            if (e.ip.s_addr != 0) {
                ip_texto(e.ip.s_addr, rede.dns, sizeof rede.dns);
                break;
            }
        }
    }

    rede.classe = gh_classificar_dns(rede.dns, rede.gateway);
}

/* Chamar isto é o único momento em que a senha existe fora do sistema. Ela
   vai para uma variável, é enviada e some quando o app fecha -- nunca para a
   tela, nunca para o cartão. */
static void ler_credencial(void)
{
    tem_credencial = 0;
    meu_pid = 0;
    minha_senha[0] = '\0';

    if (R_FAILED(frdInit(false))) {
        snprintf(erro_credencial, sizeof erro_credencial,
                 gh_texto(GH_T_SEM_FRD, idioma));
        return;
    }

    FriendKey chave;
    Result r1 = FRD_GetMyFriendKey(&chave);

    /* Nome e Mii são independentes da credencial: mesmo num console sem conta
       NEX eles costumam existir, e mostrar quem está ali ajuda a pessoa a
       saber que o app leu o console certo. */
    MiiScreenName nome16;
    memset(nome16, 0, sizeof nome16);
    if (R_SUCCEEDED(FRD_GetMyScreenName(&nome16))) {
        ssize_t n = utf16_to_utf8((uint8_t *)meu_nome, nome16,
                                  sizeof meu_nome - 1);
        meu_nome[n > 0 ? n : 0] = '\0';
    }

    FriendMii mii;
    memset(&mii, 0, sizeof mii);
    if (R_SUCCEEDED(FRD_GetMyMii(&mii))) {
        mii_pele   = mii.miiData.face_style.skinColor;
        mii_rosto  = mii.miiData.face_style.shape;
        mii_cabelo = mii.miiData.hair_details.color;
        mii_camisa = mii.miiData.mii_details.shirt_color;
        tem_mii = 1;
        /* Sem nome de conta, o nome do Mii serve. */
        if (!meu_nome[0]) {
            /* mii_name mora numa struct empacotada; apontar direto para ela dá
               ponteiro desalinhado. Copia antes. */
            u16 alinhado[11];
            memset(alinhado, 0, sizeof alinhado);
            memcpy(alinhado, mii.miiData.mii_name, sizeof mii.miiData.mii_name);
            ssize_t n = utf16_to_utf8((uint8_t *)meu_nome, alinhado,
                                      sizeof meu_nome - 1);
            meu_nome[n > 0 ? n : 0] = '\0';
        }
    }

    static char bruto[0x800];
    memset(bruto, 0, sizeof bruto);
    Result r2 = FRD_GetMyPassword(bruto, sizeof bruto);
    frdExit();

    if (R_FAILED(r1) || R_FAILED(r2)) {
        snprintf(erro_credencial, sizeof erro_credencial,
                 gh_texto(GH_T_SEM_CRED, idioma),
                 (unsigned long)r1, (unsigned long)r2);
        memset(bruto, 0, sizeof bruto);
        return;
    }

    /* A senha NEX tem 16 caracteres ASCII imprimíveis. Qualquer coisa
       diferente disso é conta não configurada -- melhor dizer isso do que
       mandar lixo para o servidor. */
    int n = 0;
    while (n < 16 && bruto[n] >= 0x20 && bruto[n] <= 0x7E) n++;
    if (chave.principalId == 0 || n != 16) {
        snprintf(erro_credencial, sizeof erro_credencial,
                 gh_texto(GH_T_SEM_CONTA, idioma));
        memset(bruto, 0, sizeof bruto);
        return;
    }

    meu_pid = chave.principalId;
    memcpy(minha_senha, bruto, 16);
    minha_senha[16] = '\0';
    memset(bruto, 0, sizeof bruto);
    tem_credencial = 1;
}

/* Espera o socket ficar pronto para ler ou escrever, com prazo. */
#define PRAZO_MS 8000

static int esperar(int s, short evento)
{
    struct pollfd p;
    p.fd = s;
    p.events = evento;
    p.revents = 0;
    return poll(&p, 1, PRAZO_MS) > 0 && (p.revents & evento) != 0;
}

/* Um pedido HTTP, escrito na mao e mandado pelo socket.

   Antes isto usava o modulo HTTPC do 3DS, e o POST chegava no servidor com
   Content-Length: 0 -- a senha nunca saia do console. O log do servidor
   mostrou o motivo: dois "Content-Type" no mesmo pedido. O modulo montava o
   POST como formulario e ignorava os bytes que a gente entregava; o retorno
   dele, que talvez dissesse isso, a gente descartava.

   Aqui e HTTP simples na rede local, sem TLS, entao nao ha nada que o HTTPC
   resolva por nos. Escrevendo o pedido, o byte que sai do console e o mesmo
   que a suite confere no PC -- e o Content-Length passa a ser nosso.

   Devolve o codigo HTTP, ou 0 se nem chegou a falar com ninguem. */
static int http_pedir(const char *ip, int porta, const char *caminho,
                      const char *corpo, char *saida, size_t n)
{
    static char pedido[768];
    static char bruto[1024];

    if (saida && n) saida[0] = '\0';
    if (gh_montar_pedido(pedido, sizeof pedido, caminho, ip, porta, corpo) < 0)
        return 0;

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { memset(pedido, 0, sizeof pedido); return 0; }

    struct sockaddr_in destino;
    memset(&destino, 0, sizeof destino);
    destino.sin_family = AF_INET;
    destino.sin_port   = htons((u16)porta);
    destino.sin_addr.s_addr = inet_addr(ip);

    /* Nao-bloqueante com prazo: um servidor que aceita a conexao e nunca
       responde prenderia o app, e quem esta segurando o console nao teria
       como saber. A libctru do 3DS nao tem SO_RCVTIMEO -- o prazo sai do
       poll(). */
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);

    int codigo = 0;
    int ligou = connect(s, (struct sockaddr *)&destino, sizeof destino) == 0
                || (errno == EINPROGRESS && esperar(s, POLLOUT));

    if (ligou) {
        size_t total = strlen(pedido), enviado = 0;
        while (enviado < total && esperar(s, POLLOUT)) {
            int e = send(s, pedido + enviado, total - enviado, 0);
            if (e > 0) enviado += (size_t)e;
            else if (!(e < 0 && errno == EAGAIN)) break;
        }
        if (enviado == total) {
            size_t lido = 0;
            while (lido < sizeof bruto - 1 && esperar(s, POLLIN)) {
                int r = recv(s, bruto + lido, sizeof bruto - 1 - lido, 0);
                if (r > 0) lido += (size_t)r;
                else if (r == 0) break;                       /* fim limpo */
                else if (errno != EAGAIN) break;
            }
            bruto[lido] = '\0';
            const char *resposta = "";
            codigo = gh_resposta_http(bruto, &resposta);
            if (saida && n > 1) {
                strncpy(saida, resposta, n - 1);
                saida[n - 1] = '\0';
            }
        }
    }
    closesocket(s);
    memset(pedido, 0, sizeof pedido);   /* levava a senha */
    memset(bruto, 0, sizeof bruto);
    return codigo;
}

/* ---------------------------------------------------------------- perfis */

static void salvar_perfis(void)
{
    static char texto[4096];
    if (gh_perfis_escrever(texto, sizeof texto, perfis, qtd) < 0) return;
    mkdir("sdmc:/3ds", 0777);
    mkdir(PASTA, 0777);
    FILE *f = fopen(ARQUIVO, "w");
    if (!f) return;
    fputs(texto, f);
    fclose(f);
}

static void carregar_perfis(void)
{
    static char texto[4096];
    qtd = 0;
    FILE *f = fopen(ARQUIVO, "r");
    if (f) {
        size_t lido = fread(texto, 1, sizeof texto - 1, f);
        texto[lido] = '\0';
        fclose(f);
        qtd = gh_perfis_ler(texto, perfis, GH_MAX_PERFIS);
    }

    /* Primeira vez com um DNS personalizado: já deixa a guilda pronta na
       lista, com o padrão. Quase sempre é exatamente o que a pessoa quer. */
    if (rede.classe == GH_DNS_CANDIDATO
        && gh_perfil_achar(perfis, qtd, rede.dns, GH_PORTA_PADRAO, GH_ROTA_PADRAO) < 0
        && qtd < GH_MAX_PERFIS) {
        GhPerfil p;
        memset(&p, 0, sizeof p);
        snprintf(p.nome, sizeof p.nome, gh_texto(GH_T_NOME_PADRAO, idioma));
        snprintf(p.ip, sizeof p.ip, "%s", rede.dns);
        snprintf(p.rota, sizeof p.rota, "%s", GH_ROTA_PADRAO);
        p.porta = GH_PORTA_PADRAO;
        p.estado = GH_PERFIL_NOVO;
        perfis[qtd++] = p;
        salvar_perfis();
    }
}

static int perfil_atual_e_o_dns(int i)
{
    return rede.classe == GH_DNS_CANDIDATO && strcmp(perfis[i].ip, rede.dns) == 0;
}

/* ------------------------------------------------------------- teclado -- */

static int perguntar(const char *dica, const char *inicial, char *saida, size_t n,
                     SwkbdType tipo)
{
    SwkbdState kb;
    char buf[128];

    som_tocar(GH_SOM_CONFIRMA);

    if (n > sizeof buf) n = sizeof buf;
    swkbdInit(&kb, tipo, 2, (int)n - 1);
    swkbdSetHintText(&kb, dica);
    if (inicial && inicial[0]) swkbdSetInitialText(&kb, inicial);
    swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);

    buf[0] = '\0';
    if (swkbdInputText(&kb, buf, sizeof buf) != SWKBD_BUTTON_CONFIRM) return 0;
    snprintf(saida, n, "%s", buf);
    return 1;
}

/* Mensagem com um som escolhido a dedo, para os casos em que a cor nao diz
   tudo: "achei o cadastro" e "nao achei" sao os dois desfechos da consulta, e
   nenhum dos dois e erro. */
static void dizer_som(int som, u32 cor, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(recado, sizeof recado, fmt, ap);
    va_end(ap);
    cor_recado = cor;
    som_tocar(som);
}

static void dizer(u32 cor, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(recado, sizeof recado, fmt, ap);
    va_end(ap);
    cor_recado = cor;

    /* Só o desfecho sai da cor: vermelho é falha, verde é sucesso. Confirmar e
       selecionar são AÇÕES, e cada ação escolhe o próprio som -- tratar as
       duas como "a cor é ouro" fazia o toque numa guilda soar como confirmação. */
    if (cor == COR_CARMIM)     som_tocar(GH_SOM_ERRO);
    else if (cor == COR_VERDE) som_tocar(GH_SOM_SUCESSO);
}

/* --------------------------------------------------------------- telas -- */

static void desenhar_topo_tela(const char *ocupado)
{
    C2D_TargetClear(alvo_topo, COR_NOITE);
    C2D_SceneBegin(alvo_topo);

    emblema(34, 32, 18);
    escrever(62, 14, 0.85f, COR_CLARA, "GUILD HUNTER");
    escrever(62, 36, 0.44f, COR_OURO, gh_texto(GH_T_SUBTITULO, idioma));
    C2D_DrawLine(12, 58, COR_CARMIM, LARG_TOPO - 12, 58, COR_CARMIM, 2.0f, 0.2f);

    /* Ficha do caçador */
    escrever(14, 64, 0.44f, COR_CINZA, gh_texto(GH_T_CACADOR, idioma));
    retrato(LARG_TOPO - 40, 88, 24);

    if (meu_nome[0])
        escrever(14, 76, 0.62f, COR_CLARA, "%s", meu_nome);
    else
        escrever(14, 76, 0.5f, COR_CINZA, gh_texto(GH_T_SEM_NOME, idioma));

    if (tem_credencial) {
        escrever(14, 96, 0.44f, COR_CINZA, "ID %lu", (unsigned long)meu_pid);
    } else {
        escrever(14, 96, 0.42f, COR_CARMIM, "%s", erro_credencial);
    }

    escrever(14, 118, 0.44f, COR_CINZA, gh_texto(GH_T_REDE_ROTULO, idioma));
    escrever(14, 130, 0.5f, COR_CLARA, gh_texto(GH_T_CONSOLE, idioma),
             rede.ip[0] ? rede.ip : gh_texto(GH_T_SEM_ENDERECO, idioma));
    escrever(200, 130, 0.5f, COR_CLARA, gh_texto(GH_T_ROTEADOR, idioma),
             rede.gateway[0] ? rede.gateway : "?");

    u32 cor_dns = rede.classe == GH_DNS_CANDIDATO ? COR_VERDE : COR_CARMIM;
    if (rede.dns[0])
        escrever(14, 146, 0.5f, cor_dns, "DNS %s  (%s)",
                 rede.dns, gh_dns_rotulo_i(rede.classe, idioma));
    else
        escrever(14, 146, 0.5f, cor_dns, gh_texto(GH_T_DNS_NAO_VISTO, idioma));
    escrever(14, 162, 0.42f, COR_CINZA, "%s", gh_dns_explica_i(rede.classe, idioma));
    /* Isto é leitura de sinais, não uma chave que o console exponha -- e a
       pessoa merece saber a diferença antes de sair mexendo na configuracao. */
    if (rede.classe == GH_DNS_ROTEADOR)
        escrever(14, 176, 0.4f, COR_CINZA,
                 gh_texto(GH_T_DEDUZIDO, idioma));

    C2D_DrawLine(12, 186, COR_CARMIM, LARG_TOPO - 12, 186, COR_CARMIM, 1.0f, 0.2f);
    if (ocupado)
        escrever(14, 194, 0.55f, COR_OURO, "%s", ocupado);
    else if (recado[0])
        escrever(14, 194, 0.5f, cor_recado, "%s", recado);

    /* Uma linha só. Antes eram três blocos miúdos espremidos em 50 pixels --
       dicas, SELECT e a nota, cada um numa altura -- e o resultado era ruído
       em vez de informação. O que a pessoa precisa saber cabe numa linha. */
    if (modal)
        escrever(14, 214, 0.42f, COR_CINZA, gh_texto(GH_T_DICA_MODAL, idioma));
    else if (tela == TELA_FORM)
        escrever(14, 214, 0.42f, COR_CINZA, gh_texto(GH_T_DICA_FORM, idioma));
    else
        escrever(14, 214, 0.42f, COR_CINZA,
                 gh_texto(GH_T_DICA_LISTA, idioma));

    /* SELECT em cima da nota, no canto: empilhados ocupam menos largura e
       deixam a linha de dicas respirar. */
    int tocando = som_disponivel() && musica_existe() && musica_tocando();
    escrever(338, 196, 0.40f, COR_CINZA, "SELECT");
    icone_musica(357, 222, tocando ? COR_OURO : COR_CINZA, !tocando);
}

static void desenhar_base_tela(void)
{
    int ligado = (rede.classe == GH_DNS_CANDIDATO);

    C2D_TargetClear(alvo_base, COR_PERGAMINHO);
    C2D_SceneBegin(alvo_base);

    C2D_DrawRectSolid(0, 0, 0.1f, LARG_BASE, 26, COR_TINTA);
    escrever(10, 4, 0.55f, COR_CLARA, gh_texto(GH_T_GUILDAS, idioma));
    if (!ligado) {
        losango(304, 13, 8, COR_CINZA);
        barrado(304, 13, 8, COR_CARMIM);
        escrever(150, 6, 0.44f, COR_OURO, "%s",
                 rede.classe == GH_DNS_AUSENTE ? gh_texto(GH_T_SEM_REDE, idioma)
                 : rede.classe == GH_DNS_PUBLICO ? gh_texto(GH_T_PUB_CURTO, idioma)
                 : gh_texto(GH_T_PERS_DESLIG, idioma));
    } else {
        int n = 0;
        for (int i = 0; i < qtd; i++) n += marcada[i] ? 1 : 0;
        if (n) escrever(232, 6, 0.44f, COR_OURO, gh_texto(GH_T_MARCADAS, idioma), n, n > 1 ? "s" : "");
        else   escrever(232, 6, 0.44f, COR_CLARA, gh_texto(GH_T_QUANTAS, idioma), qtd, qtd > 1 ? "s" : "");
    }

    if (qtd == 0) {
        escrever_centro(LARG_BASE / 2, 80, 0.5f, COR_CINZA, gh_texto(GH_T_LISTA_VAZIA, idioma));
        escrever_centro(LARG_BASE / 2, 100, 0.45f, COR_CINZA,
                        gh_texto(GH_T_LISTA_DICA, idioma));
    }

    for (int v = 0; v < LINHAS_VISIVEIS; v++) {
        int i = rolagem + v;
        if (i >= qtd) break;
        Caixa c = { 8, LINHA_Y0 + v * LINHA_ALT, LARG_BASE - 16, LINHA_ALT - 4 };
        int marcado = (foco == i);
        moldura(c, marcado ? COR_PAPEL : COR_PERGAMINHO,
                marcado ? COR_CARMIM : COR_CINZA, marcado);

        caixa_selecao(c.x + 6, c.y + c.a / 2 - 7, marcada[i], marcado);
        icone_perfil(c.x + 38, c.y + c.a / 2, perfis[i].estado,
                     perfil_atual_e_o_dns(i), ligado);
        escrever(c.x + 56, c.y + 2, 0.5f, COR_TINTA, "%s", perfis[i].nome);
        escrever(c.x + 56, c.y + 15, 0.4f, COR_CINZA, "%s:%d%s",
                 perfis[i].ip, perfis[i].porta, perfis[i].rota);
    }

    for (int b = 0; b < N_BOTOES; b++) {
        int marcado = (foco == qtd + b);
        /* Sondar e Cadastrar não fazem sentido sem uma guilda escolhida. */
        int vivo = (b >= BT_NOVA) || (qtd > 0);
        moldura(BOTOES[b], marcado ? COR_CARMIM : COR_PAPEL,
                marcado ? COR_OURO : COR_CINZA, marcado);
        escrever_centro(BOTOES[b].x + BOTOES[b].l / 2, BOTOES[b].y + 7, 0.52f,
                        !vivo ? COR_CINZA : (marcado ? COR_CLARA : COR_TINTA),
                        "%s", gh_texto(ROTULO[b], idioma));
    }
}

/* O que aparece no campo. A porta vira texto aqui para os quatro campos serem
   tratados igual no desenho e na navegacao. */
static const char *valor_do_campo(int c, char *tmp, size_t n)
{
    switch (c) {
    case CAMPO_NOME:  return rascunho.nome;
    case CAMPO_IP:    return rascunho.ip;
    case CAMPO_PORTA: snprintf(tmp, n, "%d", rascunho.porta); return tmp;
    case CAMPO_ROTA:  return rascunho.rota;
    }
    return "";
}

/* Campo por campo, para o erro aparecer ONDE ele esta em vez de numa mensagem
   generica no fim. Vazio nao conta como errado: e so o que falta preencher. */
static int campo_ok(int c)
{
    switch (c) {
    case CAMPO_NOME:  return rascunho.nome[0] != '\0';
    case CAMPO_IP:    return gh_ip_valido(rascunho.ip);
    case CAMPO_PORTA: return rascunho.porta >= 1 && rascunho.porta <= 65535;
    case CAMPO_ROTA:  return gh_rota_valida(rascunho.rota);
    }
    return 0;
}

static void desenhar_form_tela(void)
{
    char tmp[16];

    C2D_TargetClear(alvo_base, COR_PERGAMINHO);
    C2D_SceneBegin(alvo_base);

    C2D_DrawRectSolid(0, 0, 0.1f, LARG_BASE, 28, COR_TINTA);
    escrever(10, 5, 0.55f, COR_CLARA, "%s",
             form_alvo < 0 ? gh_texto(GH_T_FORM_NOVA, idioma) : gh_texto(GH_T_FORM_EDITAR, idioma));
    escrever(196, 8, 0.42f, COR_OURO, gh_texto(GH_T_TOQUE_CAMPO, idioma));

    for (int c = 0; c < N_CAMPOS; c++) {
        Caixa cx = { 8, CAMPO_Y0 + c * CAMPO_ALT, LARG_BASE - 16, CAMPO_ALT - 6 };
        int marcado = (form_foco == c);
        int vazio = (valor_do_campo(c, tmp, sizeof tmp)[0] == '\0');
        int ruim = !vazio && !campo_ok(c);

        moldura(cx, marcado ? COR_PAPEL : COR_PERGAMINHO,
                ruim ? COR_CARMIM : (marcado ? COR_CARMIM : COR_CINZA), marcado);
        escrever(cx.x + 8, cx.y + 3, 0.42f, COR_CINZA, "%s", gh_texto(CAMPO_ROTULO[c], idioma));

        const char *v = valor_do_campo(c, tmp, sizeof tmp);
        escrever(cx.x + 8, cx.y + 15, 0.5f,
                 vazio ? COR_CINZA : (ruim ? COR_CARMIM : COR_TINTA),
                 "%s", vazio ? gh_texto(GH_T_VAZIO, idioma) : v);
    }

    /* Salvar so acende quando tudo esta preenchido e valido: e mais honesto
       do que deixar apertar e recusar depois. */
    int pronto = 1;
    for (int c = 0; c < N_CAMPOS; c++) if (!campo_ok(c)) pronto = 0;

    for (int b = 0; b < 2; b++) {
        int marcado = (form_foco == N_CAMPOS + b);
        int vivo = (b == 1) || pronto;
        moldura(BOTOES_FORM[b], marcado ? COR_CARMIM : COR_PAPEL,
                marcado ? COR_OURO : COR_CINZA, marcado);
        escrever_centro(BOTOES_FORM[b].x + BOTOES_FORM[b].l / 2,
                        BOTOES_FORM[b].y + 9, 0.55f,
                        !vivo ? COR_CINZA : (marcado ? COR_CLARA : COR_TINTA),
                        "%s", b == 0 ? gh_texto(GH_T_SALVAR, idioma) : gh_texto(GH_T_SAIR, idioma));
    }
}

static void desenhar_modal(void)
{
    /* Tudo do modal sobe de camada: senão o formulário atrás desenha com a
       mesma profundidade e atravessa a caixa -- o endereço de trás aparecia
       escrito por cima da pergunta. */
    camada = 0.35f;

    /* Escurece o que está atrás sem apagar: a pessoa continua vendo o
       formulário, mas o olho vai para a pergunta. */
    C2D_DrawRectSolid(0, 0, 0.36f, LARG_BASE, ALTURA,
                      C2D_Color32(0x14, 0x11, 0x0E, 0xD8));

    /* Sombra: separa a caixa do fundo mesmo onde as cores se parecem. */
    Caixa sombra = { CAIXA_MODAL.x + 4, CAIXA_MODAL.y + 5,
                     CAIXA_MODAL.l, CAIXA_MODAL.a };
    C2D_DrawRectSolid(sombra.x, sombra.y, 0.4f, sombra.l, sombra.a,
                      C2D_Color32(0x00, 0x00, 0x00, 0x80));

    moldura(CAIXA_MODAL, COR_PERGAMINHO, COR_CARMIM, 1);
    /* Fio de ouro por dentro, como as fichas do balcão. */
    Caixa fio = { CAIXA_MODAL.x + 4, CAIXA_MODAL.y + 4,
                  CAIXA_MODAL.l - 8, CAIXA_MODAL.a - 8 };
    C2D_DrawLine(fio.x, fio.y, COR_OURO, fio.x + fio.l, fio.y, COR_OURO,
                 1.0f, 0.55f + camada);
    C2D_DrawLine(fio.x, fio.y + fio.a, COR_OURO, fio.x + fio.l, fio.y + fio.a,
                 COR_OURO, 1.0f, 0.55f + camada);

    if (modal == MODAL_APAGAR) {
        int alvo[GH_MAX_PERFIS];
        int n = alvos(alvo);
        if (n == 1)
            escrever_centro(LARG_BASE / 2, CAIXA_MODAL.y + 16, 0.6f, COR_TINTA,
                            gh_texto(GH_T_Q_APAGAR1, idioma));
        else
            escrever_centro(LARG_BASE / 2, CAIXA_MODAL.y + 16, 0.6f, COR_TINTA,
                            gh_texto(GH_T_Q_APAGARN, idioma), n);
        if (n == 1)
            escrever_centro(LARG_BASE / 2, CAIXA_MODAL.y + 42, 0.42f, COR_CINZA,
                            "%s   %s:%d", perfis[alvo[0]].nome,
                            perfis[alvo[0]].ip, perfis[alvo[0]].porta);
        else
            escrever_centro(LARG_BASE / 2, CAIXA_MODAL.y + 42, 0.42f, COR_CINZA,
                            gh_texto(GH_T_SEM_VOLTA, idioma));
    } else if (modal == MODAL_SAIR) {
        escrever_centro(LARG_BASE / 2, CAIXA_MODAL.y + 16, 0.6f, COR_TINTA,
                        gh_texto(GH_T_Q_SAIR, idioma));
        escrever_centro(LARG_BASE / 2, CAIXA_MODAL.y + 42, 0.42f, COR_CINZA,
                        gh_texto(GH_T_LISTA_FICA, idioma));
    } else {
        escrever_centro(LARG_BASE / 2, CAIXA_MODAL.y + 16, 0.6f, COR_TINTA,
                        gh_texto(GH_T_Q_SALVAR, idioma));
        escrever_centro(LARG_BASE / 2, CAIXA_MODAL.y + 42, 0.42f, COR_CINZA,
                        "%s:%d%s", rascunho.ip, rascunho.porta, rascunho.rota);
    }

    for (int b = 0; b < 2; b++) {
        int marcado = (modal_foco == b);
        moldura(BOTOES_MODAL[b], marcado ? COR_CARMIM : COR_PAPEL,
                marcado ? COR_OURO : COR_CINZA, marcado);
        escrever_centro(BOTOES_MODAL[b].x + BOTOES_MODAL[b].l / 2,
                        BOTOES_MODAL[b].y + 9, 0.55f,
                        marcado ? COR_CLARA : COR_TINTA, "%s", b == 0 ? gh_texto(GH_T_SIM, idioma) : gh_texto(GH_T_NAO, idioma));
    }

    camada = 0.0f;
}

static void desenhar(const char *ocupado)
{
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(buf_texto);
    desenhar_topo_tela(ocupado);
    so_caixas = (modal != MODAL_NENHUM);   /* só as caixas vazias atrás */
    if (tela == TELA_FORM) desenhar_form_tela();
    else                   desenhar_base_tela();
    so_caixas = 0;
    if (modal) desenhar_modal();
    C3D_FrameEnd(0);
}

/* --------------------------------------------------------------- ações -- */

static int perfil_escolhido(void)
{
    if (qtd == 0) return -1;
    return (foco < qtd) ? foco : -1;
}

/* Quando o foco está num botão, a guilda que vale é a última que estava
   marcada na lista -- senão os botões nunca teriam alvo. */
static int alvo_da_acao(void)
{
    static int ultimo = 0;
    int e = perfil_escolhido();
    if (e >= 0) ultimo = e;
    if (ultimo >= qtd) ultimo = qtd - 1;
    return qtd ? ultimo : -1;
}

/* Sonda a rota e, de quebra, pergunta se este console ja esta cadastrado la.
   Sao a mesma ida: um GET, duas respostas. E o segundo pedaco e o que salva
   quem formatou o cartao e perdeu a lista -- da para remontar o historico
   sem reenviar a credencial para ninguem. */
/* Quem a ação vai atingir: as marcadas; nenhuma marcada, a que está em foco.

   O padrão de "nenhuma marcada = a de foco" existe para o caso de uma guilda
   só, que é o comum -- obrigar a marcar antes de cadastrar seria burocracia
   para quem tem uma linha na lista. */
static int alvos(int *saida)
{
    int n = 0;
    for (int i = 0; i < qtd; i++)
        if (marcada[i]) saida[n++] = i;
    if (n == 0) {
        int i = alvo_da_acao();
        if (i >= 0) saida[n++] = i;
    }
    return n;
}

static void acao_sondar(void)
{
    int alvo[GH_MAX_PERFIS];
    int n = alvos(alvo);
    if (n == 0) { dizer(COR_CARMIM, gh_texto(GH_T_SEM_SONDAR, idioma)); return; }

    int achou = 0, cadastrado = 0, falhou = 0;
    char caminho[96], corpo[512];

    for (int k = 0; k < n; k++) {
        int i = alvo[k];
        int montou = gh_montar_caminho(caminho, sizeof caminho, perfis[i].rota,
                                       tem_credencial ? meu_pid : 0);
        if (montou < 0) { perfis[i].estado = GH_PERFIL_FALHOU; falhou++; continue; }

        char aviso[96];
        snprintf(aviso, sizeof aviso, gh_texto(GH_T_SONDANDO, idioma),
                 perfis[i].nome, k + 1, n);
        desenhar(aviso);

        int http = http_pedir(perfis[i].ip, perfis[i].porta, caminho,
                              NULL, corpo, sizeof corpo);
        if (!gh_sonda_ok(http, corpo)) {
            perfis[i].estado = GH_PERFIL_FALHOU;
            falhou++;
            continue;
        }
        if (gh_consulta_conhecido(corpo) == 1) {
            perfis[i].estado = GH_PERFIL_CADASTRADO;
            cadastrado++;
        } else {
            perfis[i].estado = GH_PERFIL_SONDADO;
            achou++;
        }
    }
    salvar_perfis();

    if (n == 1) {
        int i = alvo[0];
        if (falhou)          dizer(COR_CARMIM, gh_texto(GH_T_NINGUEM, idioma),
                                   perfis[i].ip, perfis[i].porta);
        else if (cadastrado) dizer_som(GH_SOM_ACHOU, COR_VERDE,
                                       gh_texto(GH_T_JA_AQUI, idioma));
        else                 dizer_som(GH_SOM_NAO_ACHOU, COR_OURO,
                                       gh_texto(GH_T_ROTA_EXISTE, idioma));
        return;
    }
    if (falhou == n)
        dizer(COR_CARMIM, gh_texto(GH_T_NENHUMA_RESP, idioma), n);
    else
        dizer_som(cadastrado ? GH_SOM_ACHOU : GH_SOM_NAO_ACHOU,
                  falhou ? COR_OURO : COR_VERDE,
                  gh_texto(GH_T_RESUMO_SONDA, idioma),
                  cadastrado, cadastrado == 1 ? "" : "s", achou, falhou);
}

static void acao_cadastrar(void)
{
    int alvo[GH_MAX_PERFIS];
    int n = alvos(alvo);
    if (n == 0) { dizer(COR_CARMIM, gh_texto(GH_T_NENHUMA, idioma)); return; }
    if (!tem_credencial) { dizer(COR_CARMIM, "%s", erro_credencial); return; }

    char caminho[96], corpo[256], resposta[512];
    int aceitos = 0, ja = 0, recusados = 0;
    GhResultado ultimo = GH_RES_REDE;

    for (int k = 0; k < n; k++) {
        int i = alvo[k];
        if (gh_montar_caminho(caminho, sizeof caminho, perfis[i].rota, 0) < 0
            || gh_montar_corpo(corpo, sizeof corpo, meu_pid, minha_senha, "") < 0) {
            perfis[i].estado = GH_PERFIL_FALHOU;
            recusados++;
            /* Nao chegamos a falar com ninguem, mas tambem nao foi a rede:
               dizer "nao consegui falar com o servidor" aqui mandaria a
               pessoa mexer no roteador por um problema que e daqui. */
            ultimo = GH_RES_SEM_MONTAR;
            continue;
        }

        char aviso[96];
        snprintf(aviso, sizeof aviso, gh_texto(GH_T_CADASTRANDO, idioma),
                 perfis[i].nome, k + 1, n);
        desenhar(aviso);

        int http = http_pedir(perfis[i].ip, perfis[i].porta, caminho,
                              corpo, resposta, sizeof resposta);
        memset(corpo, 0, sizeof corpo);      /* a senha não fica na pilha */

        ultimo = gh_interpretar(http, resposta);
        if (ultimo == GH_RES_REGISTRADO)      { perfis[i].estado = GH_PERFIL_CADASTRADO; aceitos++; }
        else if (ultimo == GH_RES_JA_TINHA)   { perfis[i].estado = GH_PERFIL_CADASTRADO; ja++; }
        else                                  { perfis[i].estado = GH_PERFIL_FALHOU; recusados++; }
    }
    salvar_perfis();

    if (n == 1) {
        if (ultimo == GH_RES_REGISTRADO || ultimo == GH_RES_JA_TINHA)
            dizer(COR_VERDE, "%s", gh_resultado_texto_i(ultimo, idioma));
        else
            dizer(COR_CARMIM, "%s", gh_resultado_texto_i(ultimo, idioma));
        return;
    }
    if (aceitos + ja == 0)
        dizer(COR_CARMIM, gh_texto(GH_T_NENHUMA_ACEITOU, idioma), n);
    else
        dizer(recusados ? COR_OURO : COR_VERDE,
              gh_texto(GH_T_RESUMO_CAD, idioma),
              aceitos, aceitos == 1 ? "" : "s", ja, recusados);
}

static int  dentro(Caixa c, int x, int y);
static void ajustar_rolagem(void);

/* Abre o formulario. `indice` < 0 cria uma guilda nova; caso contrario carrega
   a existente, que e o que faz "Editar guilda" mostrar os campos preenchidos. */
static void abrir_form(int indice)
{
    if (indice < 0 && qtd >= GH_MAX_PERFIS) {
        dizer(COR_CARMIM, gh_texto(GH_T_CHEIA, idioma));
        return;
    }

    form_alvo = indice;
    form_foco = CAMPO_NOME;

    if (indice >= 0) {
        rascunho = perfis[indice];
    } else {
        memset(&rascunho, 0, sizeof rascunho);
        /* O que dá para adivinhar já vem preenchido: o DNS em uso quase sempre
           É a guilda, e a porta e a rota têm padrão. Sobra o nome. */
        if (rede.classe == GH_DNS_CANDIDATO)
            snprintf(rascunho.ip, sizeof rascunho.ip, "%s", rede.dns);
        snprintf(rascunho.rota, sizeof rascunho.rota, "%s", GH_ROTA_PADRAO);
        rascunho.porta = GH_PORTA_PADRAO;
        rascunho.estado = GH_PERFIL_NOVO;
    }

    tela = TELA_FORM;
    dizer_som(GH_SOM_EDITAR, COR_OURO,
              indice < 0 ? gh_texto(GH_T_PREENCHA, idioma)
                         : gh_texto(GH_T_TOQUE_MUDAR, idioma));
}

static void editar_campo(int c)
{
    /* Escreve direto no campo: perguntar() ja limita pelo tamanho que recebe e
       nao toca no destino quando a pessoa cancela. Uma copia intermediaria so
       criaria um truncamento a mais para errar. */
    switch (c) {
    case CAMPO_NOME:
        perguntar(gh_texto(GH_T_P_NOME, idioma), rascunho.nome, rascunho.nome,
                  sizeof rascunho.nome, SWKBD_TYPE_NORMAL);
        break;

    case CAMPO_IP:
        perguntar(gh_texto(GH_T_P_IP, idioma), rascunho.ip, rascunho.ip,
                  sizeof rascunho.ip, SWKBD_TYPE_NUMPAD);
        if (rascunho.ip[0] && !gh_ip_valido(rascunho.ip))
            dizer(COR_CARMIM, gh_texto(GH_T_IP_INVALIDO, idioma));
        break;

    case CAMPO_PORTA: {
        char atual[12];
        snprintf(atual, sizeof atual, "%d", rascunho.porta);
        if (perguntar(gh_texto(GH_T_P_PORTA, idioma), atual, atual,
                      sizeof atual, SWKBD_TYPE_NUMPAD))
            rascunho.porta = atoi(atual);
        if (rascunho.porta < 1 || rascunho.porta > 65535)
            dizer(COR_CARMIM, gh_texto(GH_T_PORTA_FAIXA, idioma));
        break;
    }

    case CAMPO_ROTA: {
        char digitada[GH_TAM_ROTA], limpa[GH_TAM_ROTA];
        snprintf(digitada, sizeof digitada, "%s", rascunho.rota);
        if (perguntar(gh_texto(GH_T_P_ROTA, idioma), rascunho.rota, digitada,
                      sizeof digitada, SWKBD_TYPE_QWERTY)) {
            if (gh_rota_normalizar(digitada, limpa, sizeof limpa))
                snprintf(rascunho.rota, sizeof rascunho.rota, "%s", limpa);
            else
                dizer(COR_CARMIM, gh_texto(GH_T_ROTA_INVALIDA, idioma));
        }
        break;
    }
    }
}

/* Confere tudo e abre a pergunta. Gravar de verdade só depois do Sim. */
static void pedir_confirmacao(void)
{
    for (int c = 0; c < N_CAMPOS; c++)
        if (!campo_ok(c)) {
            form_foco = c;
            dizer(COR_CARMIM, gh_texto(GH_T_FALTA_CAMPO, idioma), gh_texto(CAMPO_ROTULO[c], idioma));
            return;
        }

    int igual = gh_perfil_achar(perfis, qtd, rascunho.ip, rascunho.porta,
                                rascunho.rota);
    if (igual >= 0 && igual != form_alvo) {
        dizer(COR_CARMIM, gh_texto(GH_T_JA_NA_LISTA, idioma));
        return;
    }

    modal = MODAL_SALVAR;
    modal_foco = 0;
    som_tocar(GH_SOM_CONFIRMA);
}

static void salvar_form(void)
{
    if (form_alvo < 0) {
        perfis[qtd] = rascunho;
        foco = qtd;
        qtd++;
    } else {
        /* Mudou o endereço: o que sabíamos do anterior não vale mais. */
        GhPerfil *v = &perfis[form_alvo];
        if (v->porta != rascunho.porta || strcmp(v->rota, rascunho.rota) != 0
            || strcmp(v->ip, rascunho.ip) != 0)
            rascunho.estado = GH_PERFIL_NOVO;
        *v = rascunho;
        foco = form_alvo;
    }

    salvar_perfis();
    modal = MODAL_NENHUM;
    tela = TELA_LISTA;
    ajustar_rolagem();
    dizer_som(GH_SOM_SALVAR, COR_OURO, gh_texto(GH_T_SALVA, idioma));
}

static void cancelar_modal(void)
{
    int era = modal;
    modal = MODAL_NENHUM;
    dizer_som(GH_SOM_MOVER, COR_OURO,
              era == MODAL_APAGAR ? gh_texto(GH_T_NAO_APAGUEI, idioma)
              : era == MODAL_SAIR ? gh_texto(GH_T_FICAMOS, idioma)
                                  : gh_texto(GH_T_NAO_SALVEI, idioma));
}

static void sair_do_form(void)
{
    modal = MODAL_NENHUM;
    tela = TELA_LISTA;
    dizer_som(GH_SOM_MOVER, COR_OURO, "%s",
              gh_texto(GH_T_SAIU_SEM_SALVAR, idioma));
}

static void confirmar_form(void)
{
    if (form_foco < N_CAMPOS)          editar_campo(form_foco);
    else if (form_foco == FORM_SALVAR) pedir_confirmacao();
    else                               sair_do_form();
}

static void pedir_sair(void)
{
    modal = MODAL_SAIR;
    modal_foco = 1;      /* no gh_texto(GH_T_NAO, idioma): ninguém quer fechar sem querer */
    som_tocar(GH_SOM_CONFIRMA);
}

static void confirmar_modal(void)
{
    if (modal_foco != 0) { cancelar_modal(); return; }
    if (modal == MODAL_APAGAR)     acao_apagar();
    else if (modal == MODAL_SAIR)  { modal = MODAL_NENHUM; quer_sair = 1; }
    else                           salvar_form();
}

static void tocar_modal(int x, int y)
{
    for (int b = 0; b < 2; b++)
        if (dentro(BOTOES_MODAL[b], x, y)) {
            modal_foco = b;
            confirmar_modal();
            return;
        }
    /* Tocar fora não fecha: fechar sem querer é o que a pergunta evita. */
}

static void tocar_form(int x, int y)
{
    for (int c = 0; c < N_CAMPOS; c++) {
        Caixa cx = { 8, CAMPO_Y0 + c * CAMPO_ALT, LARG_BASE - 16, CAMPO_ALT - 6 };
        if (dentro(cx, x, y)) { form_foco = c; editar_campo(c); return; }
    }
    for (int b = 0; b < 2; b++)
        if (dentro(BOTOES_FORM[b], x, y)) {
            form_foco = N_CAMPOS + b;
            if (b == 0) pedir_confirmacao(); else sair_do_form();
            return;
        }
}

static void pedir_apagar(void)
{
    int alvo[GH_MAX_PERFIS];
    if (alvos(alvo) == 0) {
        dizer(COR_CARMIM, gh_texto(GH_T_APAGAR_ESCOLHA, idioma));
        return;
    }
    modal = MODAL_APAGAR;
    modal_foco = 1;      /* começa no gh_texto(GH_T_NAO, idioma): o gesto seguro é o padrão */
    som_tocar(GH_SOM_CONFIRMA);
}

static void acao_apagar(void)
{
    int alvo[GH_MAX_PERFIS];
    int n = alvos(alvo);
    modal = MODAL_NENHUM;
    if (n == 0) return;

    /* De trás para a frente: apagar do começo remexeria os índices que ainda
       faltam apagar. As marcas andam junto com as guildas, senão sobraria
       marca apontando para quem não é mais aquela guilda. */
    for (int k = n - 1; k >= 0; k--) {
        int i = alvo[k];
        for (int j = i; j + 1 < qtd; j++) {
            perfis[j] = perfis[j + 1];
            marcada[j] = marcada[j + 1];
        }
        qtd--;
        marcada[qtd] = 0;
    }
    if (foco >= qtd + N_BOTOES) foco = qtd + N_BOTOES - 1;
    salvar_perfis();
    ajustar_rolagem();
    dizer_som(GH_SOM_CONFIRMA, COR_OURO,
              n == 1 ? gh_texto(GH_T_REMOVIDA, idioma) : gh_texto(GH_T_REMOVIDAS, idioma), n);
}

static void confirmar(void)
{
    if (foco < qtd) {
        marcada[foco] = 1;
        dizer_som(GH_SOM_MOVER, COR_OURO, gh_texto(GH_T_MARCADA, idioma), perfis[foco].nome);
        return;
    }
    /* Sem som aqui de propósito. Toda ação destes botões termina falando --
       "achei o cadastro", "ninguém respondeu", o formulário abrindo -- e cada
       uma toca o próprio som. Um som de confirmação junto virava dois tocando
       ao mesmo tempo, um por cima do outro. Quem aperta o botão ouve o
       RESULTADO, que é a informação; ouvir "apertei" não acrescenta nada. */
    switch (foco - qtd) {
    case BT_SONDAR:    acao_sondar();       break;
    case BT_CADASTRAR: acao_cadastrar();    break;
    case BT_NOVA:      abrir_form(-1);          break;
    case BT_ROTA:      abrir_form(alvo_da_acao()); break;
    }
}

/* ------------------------------------------------------------ navegação  */

static void ajustar_rolagem(void)
{
    if (foco >= qtd) return;
    if (foco < rolagem) rolagem = foco;
    if (foco >= rolagem + LINHAS_VISIVEIS) rolagem = foco - LINHAS_VISIVEIS + 1;
    if (rolagem < 0) rolagem = 0;
}

static void mover(int delta)
{
    int total = qtd + N_BOTOES;
    som_tocar(GH_SOM_MOVER);
    foco += delta;
    if (foco < 0) foco = total - 1;
    if (foco >= total) foco = 0;
    ajustar_rolagem();
}

static int dentro(Caixa c, int x, int y)
{
    return x >= c.x && x < c.x + c.l && y >= c.y && y < c.y + c.a;
}

static void tocar(int x, int y)
{
    for (int v = 0; v < LINHAS_VISIVEIS; v++) {
        int i = rolagem + v;
        if (i >= qtd) break;
        Caixa c = { 8, LINHA_Y0 + v * LINHA_ALT, LARG_BASE - 16, LINHA_ALT - 4 };
        if (dentro(c, x, y)) {
            foco = i;
            marcada[i] = !marcada[i];       /* no toque, alternar é natural */
            dizer_som(GH_SOM_MOVER, COR_OURO,
                      gh_texto(marcada[i] ? GH_T_MARCADA : GH_T_DESMARCADA,
                               idioma), perfis[i].nome);
            return;
        }
    }
    for (int b = 0; b < N_BOTOES; b++)
        if (dentro(BOTOES[b], x, y)) {
            /* Dois tempos, como o menu do jogo: o primeiro toque escolhe, o
               segundo faz. Além de soar certo, é uma trava real no Cadastrar
               -- ele manda a credencial do console, e um toque acidental num
               botão que fica logo abaixo da lista não deveria bastar. */
            if (foco == qtd + b) {
                confirmar();          /* já selecionado: executa */
            } else {
                foco = qtd + b;
                som_tocar(GH_SOM_MOVER);
            }
            return;
        }
}

/* ------------------------------------------------------------------ main */

/* O callback do APT roda no meio da transição do sistema. Fazer trabalho
   aqui -- criar thread, abrir arquivo, esperar por outra tarefa -- trava o
   console, e foi exatamente o que aconteceu na primeira tentativa. Aqui só
   se levanta uma bandeira; quem trabalha é o laço principal, num momento em
   que o app já é dono de si de novo. */
static volatile int retomar_som;

static void ao_voltar_do_home(APT_HookType tipo, void *p)
{
    (void)p;
    if (tipo == APTHOOK_ONRESTORE || tipo == APTHOOK_ONWAKEUP)
        retomar_som = 1;
}

int main(void)
{
    romfsInit();          /* onde mora o audio embutido, se houver */
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    alvo_topo = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    alvo_base = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    buf_texto = C2D_TextBufNew(4096);

    u32 *soc = (u32 *)memalign(SOC_ALINHA, SOC_TAMANHO);
    if (soc) socInit(soc, SOC_TAMANHO);
    httpcInit(0);
    acInit();

    snprintf(erro_credencial, sizeof erro_credencial, gh_texto(GH_T_NAO_LIDA, idioma));
    escolher_idioma();
    ler_rede();
    ler_credencial();
    carregar_perfis();
    foco = qtd ? 0 : qtd + BT_NOVA;
    dizer(COR_OURO, gh_texto(GH_T_ESCOLHA, idioma));
    /* depois do primeiro dizer(), senao o app abre dando um bipe do nada */
    /* Segurar L ao abrir: sem música. É a chave de diagnóstico -- se o app
       parar de travar assim, o culpado é o streaming e a busca vai para lá. */
    hidScanInput();
    som_iniciar((hidKeysHeld() & KEY_L) != 0);

    /* O sistema toma o DSP quando o app vai para o menu HOME ou dorme. Sem
       este gancho o app volta mudo -- e mudo sem explicação, que é pior. */
    static aptHookCookie gancho;
    aptHook(&gancho, ao_voltar_do_home, NULL);

    /* Quem pediu para fechar: a pessoa, ou o sistema. Faz diferenca no fim. */
    int saida_nossa = 0;

    while (aptMainLoop()) {
        hidScanInput();
        u32 caiu = hidKeysDown();

        if (retomar_som) { retomar_som = 0; som_retomar(); }

        if ((caiu & KEY_START) && !modal) pedir_sair();
        if (quer_sair) { saida_nossa = 1; break; }
        if (caiu & KEY_SELECT) musica_alternar();

        touchPosition t;
        int tocou = 0;
        if (caiu & KEY_TOUCH) { hidTouchRead(&t); tocou = 1; }

        if (modal) {
            /* Enquanto a pergunta está na tela ela recebe tudo: deixar o
               formulário atrás continuar respondendo seria mexer no que está
               sendo confirmado. */
            if (caiu & KEY_B) cancelar_modal();
            if (caiu & (KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN)) {
                modal_foco = !modal_foco;
                som_tocar(GH_SOM_MOVER);
            }
            if (caiu & KEY_A) confirmar_modal();
            if (tocou) tocar_modal(t.px, t.py);
        } else if (tela == TELA_FORM) {
            /* No formulário o B volta para a lista em vez de fechar o app --
               sair de uma tela e sair do programa não podem ser o mesmo botão. */
            if (caiu & KEY_B) sair_do_form();
            if (caiu & (KEY_DOWN | KEY_RIGHT)) {
                form_foco = (form_foco + 1) % N_FORM_ITENS;
                som_tocar(GH_SOM_MOVER);
            }
            if (caiu & (KEY_UP | KEY_LEFT)) {
                form_foco = (form_foco + N_FORM_ITENS - 1) % N_FORM_ITENS;
                som_tocar(GH_SOM_MOVER);
            }
            if (caiu & KEY_A) confirmar_form();
            if (tocou) tocar_form(t.px, t.py);
        } else {
            /* Na lista o B desmarca; sair é no START, e passa pela pergunta.
               Um botão que às vezes desmarca e às vezes fecha o programa
               seria a pior das duas coisas. */
            if (caiu & KEY_B) {
                if (foco < qtd && marcada[foco]) {
                    marcada[foco] = 0;
                    dizer_som(GH_SOM_MOVER, COR_OURO, gh_texto(GH_T_DESMARCADA, idioma),
                              perfis[foco].nome);
                } else {
                    som_tocar(GH_SOM_MOVER);
                }
            }
            if (caiu & (KEY_DOWN | KEY_RIGHT)) mover(1);
            if (caiu & (KEY_UP | KEY_LEFT))    mover(-1);
            if (caiu & KEY_A) confirmar();
            if (caiu & KEY_X) pedir_apagar();
            if (caiu & KEY_Y) { ler_rede(); dizer(COR_OURO, gh_texto(GH_T_REDE_RELIDA, idioma)); }
            if (tocou) tocar(t.px, t.py);
        }

        musica_passo();      /* reabastece a musica */
        desenhar(NULL);
    }

    /* A despedida so existe quando e a pessoa que sai.

       Quando quem fecha e o sistema -- HOME, depois "fechar o software
       suspenso" -- o app foi suspenso e NUNCA recebeu a tela de volta:
       aptMainLoop() devolve falso sem passar por ONRESTORE. Desenhar ali
       trava, porque C3D_FrameBegin(C3D_FRAME_SYNCDRAW) fica esperando a GPU
       avisar que terminou o quadro, e a GPU agora e do menu HOME. O aviso
       nao vem, e o console para no "Fechando o software suspenso". Era isso
       nos dois aparelhos do Marcos. */
    if (saida_nossa) {
        /* Sem esta pausa o app fecha antes do som de saida sair do alto-falante. */
        som_tocar(GH_SOM_SAIR);
        desenhar(gh_texto(GH_T_ATE_A_PROXIMA, idioma));
        svcSleepThread(320000000ULL);
    }

    aptUnhook(&gancho);
    memset(minha_senha, 0, sizeof minha_senha);
    som_encerrar();

    acExit();
    httpcExit();
    socExit();
    free(soc);
    C2D_TextBufDelete(buf_texto);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    romfsExit();
    return 0;
}
