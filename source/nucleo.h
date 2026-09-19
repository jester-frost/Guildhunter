/* Guild Hunter -- o miolo, sem nada de 3DS.

   Por que este arquivo existe separado
   ------------------------------------
   Homebrew de 3DS só roda no 3DS: para ver se uma mudança funcionou seria
   preciso compilar, copiar para o cartão, abrir o Homebrew Launcher e olhar.
   Isso é lento demais para ser a única verificação -- e já nos custou caro
   antes, quando "conferi a sintaxe" virou defeito na mão do usuário.

   Então toda decisão do programa mora aqui, em C que compila no PC: como
   classificar o DNS, como montar a URL e o corpo, como ler a resposta do
   servidor, como guardar a lista de perfis. `tests/test_guild_hunter.c` roda
   isto direto na máquina. O main.c fica só com desenho e serviços do console.
*/
#ifndef GUILD_HUNTER_NUCLEO_H
#define GUILD_HUNTER_NUCLEO_H

#include <stddef.h>

#define GH_MAX_PERFIS   16
#define GH_TAM_NOME     28
#define GH_TAM_IP       16
#define GH_TAM_ROTA     64

#define GH_PORTA_PADRAO 8060
#define GH_ROTA_PADRAO  "/credencial"

/* Marca que o nosso servidor devolve no GET da rota. Sem isto, "algo
   respondeu 200" seria confundido com "a rota de cadastro existe" -- e um
   roteador qualquer responde 200 na cara. */
#define GH_MARCA        "\"servico\":\"guild-hunter\""

/* Como o DNS do console se parece. O 3DS não conta se o "DNS personalizado"
   está ligado (o ac.h só expõe proxy), então isto é inferência a partir do
   que está em uso -- e a interface diz isso com todas as letras. */
typedef enum {
    GH_DNS_AUSENTE = 0,   /* nenhum DNS em uso: sem rede ou sem configuração */
    GH_DNS_ROTEADOR,      /* DNS == gateway: veio do DHCP, personalizado desligado */
    GH_DNS_PUBLICO,       /* resolvedor público conhecido: personalizado, mas não é guilda */
    GH_DNS_CANDIDATO      /* desconhecido: pode ser uma guilda, vale sondar */
} GhDns;

/* Em que pé está um perfil da lista. */
typedef enum {
    GH_PERFIL_NOVO = 0,   /* nunca sondado */
    GH_PERFIL_SONDADO,    /* a rota de cadastro respondeu */
    GH_PERFIL_CADASTRADO, /* a credencial já foi aceita aqui */
    GH_PERFIL_FALHOU      /* a última tentativa não passou */
} GhEstado;

/* O que aconteceu numa tentativa de cadastro. */
typedef enum {
    GH_RES_REDE = 0,      /* não chegou a falar com ninguém */
    GH_RES_ROTA,          /* falou, mas a rota não existe lá */
    GH_RES_RECUSADO,      /* o servidor entendeu e disse não */
    GH_RES_CONFLITO,      /* este pid já tem outra senha registrada */
    GH_RES_JA_TINHA,      /* já estávamos cadastrados, idêntico */
    GH_RES_REGISTRADO,    /* cadastro feito agora */
    GH_RES_SEM_MONTAR     /* nem chegamos a montar o envio: problema daqui */
} GhResultado;

typedef struct {
    char nome[GH_TAM_NOME];
    char ip[GH_TAM_IP];
    int  porta;
    char rota[GH_TAM_ROTA];
    int  estado;          /* GhEstado */
} GhPerfil;

/* --- rede ------------------------------------------------------------- */

int         gh_ip_valido(const char *ip);
GhDns       gh_classificar_dns(const char *dns, const char *gateway);
const char *gh_dns_rotulo(GhDns d);
const char *gh_dns_explica(GhDns d);

/* --- montagem --------------------------------------------------------- */

int gh_rota_valida(const char *rota);
/* Arruma o que a pessoa digitou no teclado do console: poe a barra da frente
   se faltou e tira a de tras. Digitar "/" num teclado de 3DS e chato, e o
   servidor ja e tolerante com isso do lado dele. Devolve 0 se nem assim vira
   uma rota valida. */
int gh_rota_normalizar(const char *digitado, char *saida, size_t n);
/* Devolve o tamanho escrito, ou -1 se não coube / entrada inválida. */
int gh_montar_url(char *saida, size_t n, const char *ip, int porta, const char *rota);
int gh_montar_corpo(char *saida, size_t n, unsigned pid,
                    const char *senha, const char *segredo);
/* A mesma URL com `?pid=N`. Serve para perguntar "eu ja estou cadastrado
   aqui?" SEM mandar a senha -- que e o que se quer ao chegar num servidor
   desconhecido, ou ao remontar a lista depois de formatar o cartao. */
/* O caminho do pedido: a rota, e `?pid=N` quando pid nao e zero. Separado
   da URL porque quem fala por socket manda o caminho, nao a URL inteira. */
int gh_montar_caminho(char *saida, size_t n, const char *rota, unsigned pid);

/* Monta o pedido HTTP INTEIRO -- linha, cabecalhos e corpo -- pronto para
   sair pelo socket.

   Existe porque o modulo HTTPC do 3DS nao anexava o corpo: o console mandava
   POST com Content-Type repetido e Content-Length: 0, ou seja, a senha nunca
   saia do aparelho. Escrevendo o pedido aqui, o byte que sai na rede e o
   mesmo byte que a suite confere no PC.

   `corpo` nulo faz um GET. Devolve o tamanho escrito, ou -1. */
int gh_montar_pedido(char *saida, size_t n, const char *caminho,
                     const char *ip, int porta, const char *corpo);

/* Le a resposta crua do socket: devolve o codigo HTTP (0 se nao for resposta)
   e aponta `corpo` para depois da linha em branco. */
int gh_resposta_http(const char *bruto, const char **corpo);

int gh_montar_url_consulta(char *saida, size_t n, const char *ip, int porta,
                           const char *rota, unsigned pid);

/* --- leitura da resposta ---------------------------------------------- */

/* A sonda só passa com 200 E a marca do nosso servidor no corpo. */
int         gh_sonda_ok(int http, const char *corpo);
/* 1 = ja cadastrado la, 0 = nao, -1 = o servidor nao respondeu essa pergunta. */
int         gh_consulta_conhecido(const char *corpo);
GhResultado gh_interpretar(int http, const char *corpo);
const char *gh_resultado_texto(GhResultado r);

/* --- idioma ------------------------------------------------------------

   Nenhum texto de tela nasce em português dentro do código. O servidor devolve
   um CÓDIGO (o campo `estado` do JSON) e quem escolhe as palavras é o app, no
   idioma do console.

   Dois motivos. Se o texto viesse pronto do servidor, mudar a redação lá
   quebraria a tela aqui, e traduzir exigiria mexer no servidor de outra
   pessoa. E quem instalar isto num 3DS japonês ou europeu não deveria ver
   português.

   Acrescentar um idioma é acrescentar uma coluna na tabela. */
typedef enum { GH_PT = 0, GH_EN, GH_ES, GH_N_IDIOMAS } GhIdioma;

typedef enum {
    GH_T_REDE,
    GH_T_ROTA,
    GH_T_RECUSADO,
    GH_T_CONFLITO,
    GH_T_JA_TINHA,
    GH_T_REGISTRADO,
    GH_T_DNS_SEM,
    GH_T_DNS_AUTO,
    GH_T_DNS_PUB,
    GH_T_DNS_PERS,
    GH_T_DNS_SEM_X,
    GH_T_DNS_AUTO_X,
    GH_T_DNS_PUB_X,
    GH_T_DNS_PERS_X,
    GH_T_SUBTITULO,
    GH_T_CACADOR,
    GH_T_SEM_NOME,
    GH_T_REDE_ROTULO,
    GH_T_CONSOLE,
    GH_T_ROTEADOR,
    GH_T_SEM_ENDERECO,
    GH_T_DNS_NAO_VISTO,
    GH_T_DEDUZIDO,
    GH_T_GUILDAS,
    GH_T_SEM_REDE,
    GH_T_PUB_CURTO,
    GH_T_PERS_DESLIG,
    GH_T_MARCADAS,
    GH_T_QUANTAS,
    GH_T_LISTA_VAZIA,
    GH_T_LISTA_DICA,
    GH_T_BT_SONDAR,
    GH_T_BT_CADASTRAR,
    GH_T_BT_NOVA,
    GH_T_BT_EDITAR,
    GH_T_FORM_NOVA,
    GH_T_FORM_EDITAR,
    GH_T_TOQUE_CAMPO,
    GH_T_VAZIO,
    GH_T_C_NOME,
    GH_T_C_END,
    GH_T_C_PORTA,
    GH_T_C_ROTA,
    GH_T_SALVAR,
    GH_T_SAIR,
    GH_T_SIM,
    GH_T_NAO,
    GH_T_Q_SALVAR,
    GH_T_Q_APAGAR1,
    GH_T_Q_APAGARN,
    GH_T_SEM_VOLTA,
    GH_T_Q_SAIR,
    GH_T_LISTA_FICA,
    GH_T_DICA_LISTA,
    GH_T_DICA_FORM,
    GH_T_DICA_MODAL,
    GH_T_ESCOLHA,
    GH_T_MARCADA,
    GH_T_DESMARCADA,
    GH_T_NENHUMA,
    GH_T_SEM_SONDAR,
    GH_T_NINGUEM,
    GH_T_NAO_GUILDA,
    GH_T_JA_AQUI,
    GH_T_ROTA_EXISTE,
    GH_T_SEM_INFO,
    GH_T_END_INVALIDO,
    GH_T_CHEIA,
    GH_T_IP_INVALIDO,
    GH_T_PORTA_FAIXA,
    GH_T_ROTA_INVALIDA,
    GH_T_JA_NA_LISTA,
    GH_T_FALTA_CAMPO,
    GH_T_SALVA,
    GH_T_NAO_SALVEI,
    GH_T_NAO_APAGUEI,
    GH_T_FICAMOS,
    GH_T_REMOVIDA,
    GH_T_REMOVIDAS,
    GH_T_PREENCHA,
    GH_T_TOQUE_MUDAR,
    GH_T_REDE_RELIDA,
    GH_T_ATE_A_PROXIMA,
    GH_T_SONDANDO,
    GH_T_CADASTRANDO,
    GH_T_RESUMO_SONDA,
    GH_T_RESUMO_CAD,
    GH_T_NENHUMA_RESP,
    GH_T_NENHUMA_ACEITOU,
    GH_T_SEM_MONTAR,
    GH_T_SEM_FRD,
    GH_T_SEM_CRED,
    GH_T_SEM_CONTA,
    GH_T_NAO_LIDA,
    GH_T_APAGAR_ESCOLHA,
    GH_T_NOME_PADRAO,
    GH_T_P_NOME,
    GH_T_P_IP,
    GH_T_P_PORTA,
    GH_T_P_ROTA,
    GH_T_SAIU_SEM_SALVAR,
    GH_T_TOTAL
} GhTextoId;

const char *gh_texto(int id, int idioma);

/* --- ADPCM da música ---------------------------------------------------

   O DSP do 3DS toca DSP-ADPCM direto, sem a CPU decodificar. Isso encolhe a
   trilha 4x -- e, mais importante, encolhe 4x a LEITURA DE CARTÃO por bloco,
   que é o que fazia a música engasgar.

   Mora aqui, e não num script, por dois motivos: precisa ser rápido (a trilha
   tem milhões de amostras, e em Python levava minutos) e precisa ser testado
   -- codec errado não dá erro, dá barulho. */

#define GH_ADPCM_QUADRO   14      /* amostras por quadro de 8 bytes */
#define GH_ADPCM_FILTROS  8

/* Deriva os 8 filtros do próprio áudio. `saida` recebe 16 s16 (8 pares). */
void   gh_adpcm_filtros(const short *a, size_t n, short *saida);
/* Codifica. `saida` precisa de ((n+13)/14)*8 bytes. Devolve os bytes escritos. */
size_t gh_adpcm_codificar(const short *a, size_t n, const short *filtros,
                          unsigned char *saida);
/* Decodifica, continuando de (ant1, ant2) -- que ele atualiza. Serve para
   conferir e para montar a tabela de contexto de cada bloco. */
size_t gh_adpcm_decodificar(const unsigned char *d, size_t bytes,
                            const short *filtros, short *saida, size_t max,
                            short *ant1, short *ant2);
const char *gh_resultado_texto_i(GhResultado r, int idioma);
const char *gh_dns_rotulo_i(GhDns d, int idioma);
const char *gh_dns_explica_i(GhDns d, int idioma);

/* --- perfis ----------------------------------------------------------- */

int gh_perfis_ler(const char *texto, GhPerfil *saida, int max);
int gh_perfis_escrever(char *saida, size_t n, const GhPerfil *perfis, int qtd);
int gh_perfil_achar(const GhPerfil *perfis, int qtd, const char *ip, int porta,
                    const char *rota);

/* --- som ---------------------------------------------------------------

   Os sons sao SINTETIZADOS, nao gravados. Dois motivos: o audio do jogo e da
   Capcom e nao pode ser redistribuido num repositorio publico, e som calculado
   nao precisa de arquivo nenhum -- o app nao carrega nada para tocar.

   A sintese mora aqui, no lado testavel, para que o mesmo codigo que toca no
   console gere os arquivos que a gente escuta no PC. Se fossem dois codigos,
   um dia divergiriam e ninguem perceberia.

   Quem tem o jogo pode trocar por gravacoes proprias: o app le
   sdmc:/3ds/guild-hunter/sons/<nome>.wav quando existir. */

/* Os nomes de arquivo sao os que o Marcos usou ao separar os eventos -- e o
   vocabulario dele descreve melhor o que acontece do que o meu descrevia.
   Adotar os nomes dele significa que os arquivos entram sem renomear nada. */
typedef enum {
    GH_SOM_MOVER = 0,   /* select-menu-item   -- cursor andando na lista   */
    GH_SOM_CONFIRMA,    /* confirm-menu-item  -- A ou toque num botao      */
    /* O nome do arquivo vem de onde o som foi tirado; o papel aqui e outro:
       e o "Sim" do dialogo, o instante em que a guilda vai para o disco.
       Nao ha slot para "ao selecionar o aplicativo" -- nesse instante quem
       roda e o Homebrew Launcher, nao nos. */
    GH_SOM_SALVAR,      /* app-select-sound   -- "Sim, salvar"             */
    GH_SOM_EDITAR,      /* open-guild-to-edit -- teclado de edicao subindo */
    GH_SOM_ACHOU,       /* register-found     -- ja cadastrado aqui        */
    GH_SOM_NAO_ACHOU,   /* no-register-found  -- rota existe, sem cadastro */
    GH_SOM_SUCESSO,     /* quest_depart       -- cadastro concluido        */
    GH_SOM_ERRO,        /* error              -- falhou de verdade         */
    GH_SOM_SAIR,        /* exit                                            */
    GH_SOM_TOTAL
} GhSomId;

#define GH_TAXA_SOM 32000

const char *gh_som_nome(int som);
/* Escreve PCM16 mono. Devolve quantas amostras gerou, 0 se o id nao existe
   ou nao coube. Deterministico: a mesma entrada da sempre o mesmo audio. */
size_t gh_sintetizar(int som, short *saida, size_t max, unsigned taxa);

/* Quando alguem traz os proprios sons, quase nunca traz os nove. O que faltar
   NAO deve cair na sintese: misturar gravacao com som calculado soa como
   defeito -- foi exatamente o que aconteceu, um "tuk" sintetico tocando no
   meio de sons de jogo.

   Entao o que falta pega emprestado do proprio conjunto: o mais parecido que
   tenha arquivo. `tem[i]` diz quais vieram de arquivo. Devolve o indice a
   tocar, ou -1 quando nenhum serve (ai sim vale a sintese). */
int gh_som_alternativa(int som, const unsigned char *tem);

/* Acha o PCM16 mono dentro de um .wav CARREGADO INTEIRO. Devolve 1 e preenche
   as saidas, ou 0 se o arquivo nao for PCM 16 bits mono. */
int gh_wav_pcm16(const unsigned char *dados, size_t n, size_t *inicio,
                 size_t *amostras, unsigned *taxa);

/* So o cabecalho, a partir dos primeiros bytes do arquivo. Serve para tocar
   por streaming, quando carregar tudo na memoria seria absurdo -- a musica de
   fundo tem 10 MB. Nao valida o tamanho declarado do bloco de dados, porque
   ele fala do arquivo inteiro e aqui so ha o comeco. */
int gh_wav_cabecalho(const unsigned char *dados, size_t n, size_t *inicio,
                     unsigned *taxa);

#endif
