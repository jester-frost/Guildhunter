/* Guild Hunter -- o miolo. Ver nucleo.h para o porquê de estar separado. */

#include "nucleo.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------- DNS ----- */

int gh_ip_valido(const char *ip)
{
    if (!ip) return 0;
    int pontos = 0;
    for (const char *p = ip; ; ) {
        int valor = 0, digitos = 0;
        while (*p >= '0' && *p <= '9') {
            valor = valor * 10 + (*p - '0');
            if (++digitos > 3) return 0;
            p++;
        }
        if (digitos == 0 || valor > 255) return 0;
        if (*p == '.') { pontos++; p++; continue; }
        if (*p == '\0') return pontos == 3;
        return 0;
    }
}

/* Resolvedores públicos conhecidos. Não é para bloquear nada: é para o app
   poder dizer "isto é um DNS personalizado, mas não é uma guilda" em vez de
   oferecer uma sondagem que nunca vai dar em nada. */
static const char *PUBLICOS[] = {
    "8.8.8.8", "8.8.4.4",                          /* Google */
    "1.1.1.1", "1.0.0.1", "1.1.1.2", "1.0.0.2",    /* Cloudflare */
    "9.9.9.9", "149.112.112.112",                  /* Quad9 */
    "208.67.222.222", "208.67.220.220",            /* OpenDNS */
    "94.140.14.14", "94.140.15.15",                /* AdGuard */
    "76.76.2.0", "76.76.10.0",                     /* Control D */
    "185.228.168.9", "185.228.169.9",              /* CleanBrowsing */
    "77.88.8.8", "77.88.8.1",                      /* Yandex */
    "4.2.2.1", "4.2.2.2", "4.2.2.3", "4.2.2.4",    /* Level3 */
    "64.6.64.6", "64.6.65.6",                      /* Verisign */
    NULL
};

GhDns gh_classificar_dns(const char *dns, const char *gateway)
{
    if (!gh_ip_valido(dns) || strcmp(dns, "0.0.0.0") == 0)
        return GH_DNS_AUSENTE;

    /* O 3DS não conta se o "DNS personalizado" está ligado. Mas quando o DNS
       em uso é o próprio gateway, ele quase certamente veio do DHCP -- que é
       exatamente o que a opção desligada faz. */
    if (gateway && gh_ip_valido(gateway) && strcmp(dns, gateway) == 0)
        return GH_DNS_ROTEADOR;

    for (int i = 0; PUBLICOS[i]; i++)
        if (strcmp(dns, PUBLICOS[i]) == 0)
            return GH_DNS_PUBLICO;

    return GH_DNS_CANDIDATO;
}

const char *gh_dns_rotulo(GhDns d)
{
    switch (d) {
    case GH_DNS_AUSENTE:   return "sem DNS";
    case GH_DNS_ROTEADOR:  return "automático";
    case GH_DNS_PUBLICO:   return "público";
    case GH_DNS_CANDIDATO: return "personalizado";
    }
    return "?";
}

const char *gh_dns_explica(GhDns d)
{
    switch (d) {
    case GH_DNS_AUSENTE:
        return "O console não está usando DNS nenhum. Conecte-se à rede.";
    case GH_DNS_ROTEADOR:
        return "O DNS é o próprio roteador: DNS personalizado desligado.";
    case GH_DNS_PUBLICO:
        return "DNS personalizado, mas de um resolvedor público. Não é guilda.";
    case GH_DNS_CANDIDATO:
        return "DNS personalizado desconhecido. Sonde para ver se aceita cadastro.";
    }
    return "";
}

/* --------------------------------------------------------- montagem ---- */

int gh_rota_valida(const char *rota)
{
    if (!rota || rota[0] != '/') return 0;
    /* "//host/x" faria o pedido sair para outro servidor sem o usuario ver. */
    if (rota[1] == '/') return 0;
    size_t n = strlen(rota);
    /* Uma rota de so "/" e a raiz: o lugar onde QUALQUER servidor responde.
       Aceita-la abriria de volta o buraco que a marca da sonda fecha. */
    if (n < 2 || n >= GH_TAM_ROTA) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = rota[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
              || (c >= '0' && c <= '9')
              || c == '/' || c == '-' || c == '_' || c == '.' || c == '~';
        if (!ok) return 0;      /* espaco e quebra de linha caem aqui */
    }
    return 1;
}

int gh_rota_normalizar(const char *digitado, char *saida, size_t n)
{
    if (!digitado || !saida || n < 2) return 0;

    /* espaco sobrando nas pontas o teclado deixa passar facil */
    while (*digitado == ' ') digitado++;
    size_t fim = strlen(digitado);
    while (fim > 0 && digitado[fim - 1] == ' ') fim--;
    /* barra final: /credencial/ e /credencial sao o mesmo lugar */
    while (fim > 1 && digitado[fim - 1] == '/') fim--;
    if (fim == 0) return 0;

    size_t o = 0;
    if (digitado[0] != '/') { if (n < 2) return 0; saida[o++] = '/'; }
    if (o + fim >= n) return 0;
    memcpy(saida + o, digitado, fim);
    saida[o + fim] = '\0';

    return gh_rota_valida(saida);
}

int gh_montar_url(char *saida, size_t n, const char *ip, int porta, const char *rota)
{
    if (!saida || n == 0) return -1;
    if (!gh_ip_valido(ip) || porta < 1 || porta > 65535 || !gh_rota_valida(rota))
        return -1;
    int escrito = snprintf(saida, n, "http://%s:%d%s", ip, porta, rota);
    if (escrito < 0 || (size_t)escrito >= n) { saida[0] = '\0'; return -1; }
    return escrito;
}

int gh_montar_caminho(char *saida, size_t n, const char *rota, unsigned pid)
{
    if (!saida || n == 0) return -1;
    if (!gh_rota_valida(rota)) return -1;
    int escrito = pid ? snprintf(saida, n, "%s?pid=%u", rota, pid)
                      : snprintf(saida, n, "%s", rota);
    if (escrito < 0 || (size_t)escrito >= n) { saida[0] = '\0'; return -1; }
    return escrito;
}

int gh_montar_pedido(char *saida, size_t n, const char *caminho,
                     const char *ip, int porta, const char *corpo)
{
    if (!saida || n == 0) return -1;
    if (!caminho || caminho[0] != '/') return -1;
    if (!gh_ip_valido(ip) || porta < 1 || porta > 65535) return -1;

    /* Sem "Expect: 100-continue": ele faz o cliente segurar o corpo esperando
       um aval, e um servidor que fale HTTP/1.0 nunca manda esse aval. Os dois
       lados ficam parados, e na tela sai "nao consegui falar com o servidor"
       -- culpa da rede, que nao teve culpa nenhuma. */
    int escrito;
    if (corpo)
        escrito = snprintf(saida, n,
                           "POST %s HTTP/1.1\r\n"
                           "Host: %s:%d\r\n"
                           "User-Agent: guild-hunter/1\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %u\r\n"
                           "Connection: close\r\n"
                           "\r\n%s",
                           caminho, ip, porta, (unsigned)strlen(corpo), corpo);
    else
        escrito = snprintf(saida, n,
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s:%d\r\n"
                           "User-Agent: guild-hunter/1\r\n"
                           "Connection: close\r\n"
                           "\r\n",
                           caminho, ip, porta);

    if (escrito < 0 || (size_t)escrito >= n) {
        /* O pedido leva a senha; nao deixar metade dela num buffer. */
        memset(saida, 0, n);
        return -1;
    }
    return escrito;
}

int gh_resposta_http(const char *bruto, const char **corpo)
{
    if (corpo) *corpo = "";
    if (!bruto) return 0;
    if (strncmp(bruto, "HTTP/1.", 7) != 0) return 0;

    const char *p = bruto + 7;
    if (*p != '0' && *p != '1') return 0;
    p++;
    while (*p == ' ') p++;

    int codigo = 0, digitos = 0;
    while (digitos < 3 && *p >= '0' && *p <= '9') {
        codigo = codigo * 10 + (*p - '0');
        p++; digitos++;
    }
    if (digitos != 3 || codigo < 100 || codigo > 599) return 0;

    const char *fim = strstr(bruto, "\r\n\r\n");
    if (corpo && fim) *corpo = fim + 4;
    return codigo;
}

int gh_montar_url_consulta(char *saida, size_t n, const char *ip, int porta,
                           const char *rota, unsigned pid)
{
    if (!saida || n == 0) return -1;
    if (pid == 0) return -1;
    if (!gh_ip_valido(ip) || porta < 1 || porta > 65535 || !gh_rota_valida(rota))
        return -1;
    int escrito = snprintf(saida, n, "http://%s:%d%s?pid=%u", ip, porta, rota, pid);
    if (escrito < 0 || (size_t)escrito >= n) { saida[0] = '\0'; return -1; }
    return escrito;
}

/* Copia escapando o que quebraria o JSON. A senha NEX e ASCII imprimivel
   qualquer -- aspas e barra invertida acontecem. */
static int json_texto(char *dst, size_t n, const char *src)
{
    size_t o = 0;
    for (const char *p = src; *p; p++) {
        const char *esc = NULL;
        if (*p == '"')       esc = "\\\"";
        else if (*p == '\\') esc = "\\\\";
        if (esc) {
            if (o + 2 >= n) return -1;
            dst[o++] = esc[0]; dst[o++] = esc[1];
        } else {
            if (o + 1 >= n) return -1;
            dst[o++] = *p;
        }
    }
    if (o >= n) return -1;
    dst[o] = '\0';
    return (int)o;
}

int gh_montar_corpo(char *saida, size_t n, unsigned pid,
                    const char *senha, const char *segredo)
{
    if (!saida || n == 0 || pid == 0 || !senha || !senha[0]) return -1;

    char s[128], g[128];
    if (json_texto(s, sizeof s, senha) < 0) return -1;
    if (json_texto(g, sizeof g, segredo ? segredo : "") < 0) return -1;

    int escrito;
    if (g[0])
        escrito = snprintf(saida, n,
                           "{\"pid\":%u,\"senha\":\"%s\",\"segredo\":\"%s\"}", pid, s, g);
    else
        escrito = snprintf(saida, n, "{\"pid\":%u,\"senha\":\"%s\"}", pid, s);

    if (escrito < 0 || (size_t)escrito >= n) {
        /* Nao deixar meia senha num buffer que alguem possa imprimir. */
        memset(saida, 0, n);
        return -1;
    }
    return escrito;
}

/* ---------------------------------------------------------- resposta --- */

int gh_sonda_ok(int http, const char *corpo)
{
    return http == 200 && corpo && strstr(corpo, GH_MARCA) != NULL;
}

int gh_consulta_conhecido(const char *corpo)
{
    if (!corpo) return -1;
    if (strstr(corpo, "\"conhecido\":true"))  return 1;
    if (strstr(corpo, "\"conhecido\":false")) return 0;
    /* Servidor antigo, que ainda nao sabe responder isso. Dizer "nao" seria
       mentira e faria a pessoa cadastrar de novo achando que precisava. */
    return -1;
}

GhResultado gh_interpretar(int http, const char *corpo)
{
    if (http <= 0) return GH_RES_REDE;
    if (http == 404) return GH_RES_ROTA;
    if (http == 409) return GH_RES_CONFLITO;
    if (http == 200) {
        if (corpo && strstr(corpo, "\"estado\":\"registrado\""))   return GH_RES_REGISTRADO;
        if (corpo && strstr(corpo, "\"estado\":\"ja_conhecido\"")) return GH_RES_JA_TINHA;
        /* Respondeu 200 mas nao e o nosso servidor. Chamar isso de sucesso
           faria o usuario achar que esta cadastrado numa guilda que nao existe. */
        return GH_RES_ROTA;
    }
    return GH_RES_RECUSADO;
}

static const char *TEXTOS[GH_T_TOTAL][GH_N_IDIOMAS] = {
    [GH_T_REDE] = { "não consegui falar com o servidor", "could not reach the server", "no pude hablar con el servidor" },
    [GH_T_ROTA] = { "essa rota não aceita cadastro", "that route does not accept sign-ups", "esa ruta no acepta registro" },
    [GH_T_RECUSADO] = { "o servidor recusou o envio", "the server refused it", "el servidor lo rechazó" },
    [GH_T_CONFLITO] = { "esta guilda tem OUTRA credencial para você", "this guild has ANOTHER credential for you", "este gremio tiene OTRA credencial tuya" },
    [GH_T_JA_TINHA] = { "você já faz parte desta guilda", "you are already in this guild", "ya formas parte de este gremio" },
    [GH_T_REGISTRADO] = { "cadastro feito, pode entrar", "signed up, you can join", "registro hecho, ya puedes entrar" },
    [GH_T_DNS_SEM] = { "sem DNS", "no DNS", "sin DNS" },
    [GH_T_DNS_AUTO] = { "automático", "automatic", "automático" },
    [GH_T_DNS_PUB] = { "público", "public", "público" },
    [GH_T_DNS_PERS] = { "personalizado", "custom", "personalizado" },
    [GH_T_DNS_SEM_X] = { "O console não está usando DNS nenhum. Conecte-se à rede.", "The console is not using any DNS. Connect to a network.", "La consola no usa ningún DNS. Conéctate a la red." },
    [GH_T_DNS_AUTO_X] = { "O DNS é o próprio roteador: DNS personalizado desligado.", "DNS is the router itself: custom DNS is off.", "El DNS es el propio router: DNS personalizado apagado." },
    [GH_T_DNS_PUB_X] = { "DNS personalizado, mas de um resolvedor público. Não é guilda.", "Custom DNS, but a public resolver. Not a guild.", "DNS personalizado, pero de un resolutor público. No es gremio." },
    [GH_T_DNS_PERS_X] = { "DNS personalizado desconhecido. Sonde para ver se aceita cadastro.", "Unknown custom DNS. Probe it to see if it accepts sign-ups.", "DNS personalizado desconocido. Sondéalo para ver si acepta registro." },
    [GH_T_SUBTITULO] = { "cadastro de caçador nas guildas locais", "hunter sign-up at local guilds", "registro de cazador en gremios locales" },
    [GH_T_CACADOR] = { "CAÇADOR", "HUNTER", "CAZADOR" },
    [GH_T_SEM_NOME] = { "(sem nome no console)", "(no name on this console)", "(sin nombre en la consola)" },
    [GH_T_REDE_ROTULO] = { "REDE", "NETWORK", "RED" },
    [GH_T_CONSOLE] = { "console %s", "console %s", "consola %s" },
    [GH_T_ROTEADOR] = { "roteador %s", "router %s", "router %s" },
    [GH_T_SEM_ENDERECO] = { "sem endereço", "no address", "sin dirección" },
    [GH_T_DNS_NAO_VISTO] = { "DNS não detectado", "DNS not detected", "DNS no detectado" },
    [GH_T_DEDUZIDO] = { "(deduzido: o DNS em uso é o próprio roteador)", "(inferred: the DNS in use is the router itself)", "(deducido: el DNS en uso es el propio router)" },
    [GH_T_GUILDAS] = { "GUILDAS", "GUILDS", "GREMIOS" },
    [GH_T_SEM_REDE] = { "sem rede", "no network", "sin red" },
    [GH_T_PUB_CURTO] = { "DNS público, não é guilda", "public DNS, not a guild", "DNS público, no es gremio" },
    [GH_T_PERS_DESLIG] = { "DNS personalizado desligado", "custom DNS is off", "DNS personalizado apagado" },
    [GH_T_MARCADAS] = { "%d marcada%s", "%d selected%s", "%d marcado%s" },
    [GH_T_QUANTAS] = { "%d guilda%s", "%d guild%s", "%d gremio%s" },
    [GH_T_LISTA_VAZIA] = { "nenhuma guilda ainda", "no guilds yet", "aún no hay gremios" },
    [GH_T_LISTA_DICA] = { "use \"Nova guilda\" para cadastrar uma", "use \"New guild\" to add one", "usa \"Nuevo gremio\" para añadir uno" },
    [GH_T_BT_SONDAR] = { "Sondar/consultar", "Probe / check", "Sondear / consultar" },
    [GH_T_BT_CADASTRAR] = { "Cadastrar", "Sign up", "Registrar" },
    [GH_T_BT_NOVA] = { "Nova guilda", "New guild", "Nuevo gremio" },
    [GH_T_BT_EDITAR] = { "Editar guilda", "Edit guild", "Editar gremio" },
    [GH_T_FORM_NOVA] = { "NOVA GUILDA", "NEW GUILD", "NUEVO GREMIO" },
    [GH_T_FORM_EDITAR] = { "EDITAR GUILDA", "EDIT GUILD", "EDITAR GREMIO" },
    [GH_T_TOQUE_CAMPO] = { "toque num campo", "tap a field", "toca un campo" },
    [GH_T_VAZIO] = { "(vazio -- toque para escrever)", "(empty -- tap to type)", "(vacío -- toca para escribir)" },
    [GH_T_C_NOME] = { "Nome", "Name", "Nombre" },
    [GH_T_C_END] = { "Endereço", "Address", "Dirección" },
    [GH_T_C_PORTA] = { "Porta", "Port", "Puerto" },
    [GH_T_C_ROTA] = { "Rota", "Route", "Ruta" },
    [GH_T_SALVAR] = { "Salvar", "Save", "Guardar" },
    [GH_T_SAIR] = { "Sair", "Exit", "Salir" },
    [GH_T_SIM] = { "Sim", "Yes", "Sí" },
    [GH_T_NAO] = { "Não", "No", "No" },
    [GH_T_Q_SALVAR] = { "Salvar esta guilda?", "Save this guild?", "¿Guardar este gremio?" },
    [GH_T_Q_APAGAR1] = { "Apagar esta guilda?", "Delete this guild?", "¿Borrar este gremio?" },
    [GH_T_Q_APAGARN] = { "Apagar %d guildas?", "Delete %d guilds?", "¿Borrar %d gremios?" },
    [GH_T_SEM_VOLTA] = { "isto não tem volta", "this cannot be undone", "esto no tiene vuelta atrás" },
    [GH_T_Q_SAIR] = { "Sair do Guild Hunter?", "Leave Guild Hunter?", "¿Salir de Guild Hunter?" },
    [GH_T_LISTA_FICA] = { "a lista de guildas fica salva", "your guild list stays saved", "la lista de gremios queda guardada" },
    [GH_T_DICA_LISTA] = { "(A) marca  (B) desmarca  (X) apaga  (Y) rede  (START) sai", "(A) select  (B) deselect  (X) delete  (Y) network  (START) exit", "(A) marca  (B) desmarca  (X) borra  (Y) red  (START) sale" },
    [GH_T_DICA_FORM] = { "(A) confirma   (B) volta", "(A) confirm   (B) back", "(A) confirma   (B) vuelve" },
    [GH_T_DICA_MODAL] = { "(A) confirma   (B) cancela", "(A) confirm   (B) cancel", "(A) confirma   (B) cancela" },
    [GH_T_ESCOLHA] = { "escolha uma guilda e toque em Cadastrar", "pick a guild and tap Sign up", "elige un gremio y toca Registrar" },
    [GH_T_MARCADA] = { "marcada: %s", "selected: %s", "marcado: %s" },
    [GH_T_DESMARCADA] = { "desmarcada: %s", "deselected: %s", "desmarcado: %s" },
    [GH_T_NENHUMA] = { "nenhuma guilda escolhida", "no guild selected", "ningún gremio elegido" },
    [GH_T_SEM_SONDAR] = { "nenhuma guilda para sondar", "no guild to probe", "ningún gremio para sondear" },
    [GH_T_NINGUEM] = { "ninguém respondeu em %s:%d", "nobody answered at %s:%d", "nadie respondió en %s:%d" },
    [GH_T_NAO_GUILDA] = { "respondeu %d, mas não é uma guilda", "answered %d, but it is not a guild", "respondió %d, pero no es un gremio" },
    [GH_T_JA_AQUI] = { "você JÁ está cadastrado aqui -- histórico recuperado", "you are ALREADY signed up here -- history recovered", "YA estás registrado aquí -- historial recuperado" },
    [GH_T_ROTA_EXISTE] = { "a rota existe e você ainda não está cadastrado", "the route exists and you are not signed up yet", "la ruta existe y aún no estás registrado" },
    [GH_T_SEM_INFO] = { "a rota existe; este servidor não informa cadastro", "the route exists; this server does not report sign-ups", "la ruta existe; este servidor no informa registro" },
    [GH_T_END_INVALIDO] = { "endereço inválido nesta guilda", "invalid address in this guild", "dirección inválida en este gremio" },
    [GH_T_CHEIA] = { "a lista está cheia", "the list is full", "la lista está llena" },
    [GH_T_IP_INVALIDO] = { "esse IP não é válido", "that IP is not valid", "esa IP no es válida" },
    [GH_T_PORTA_FAIXA] = { "porta fora da faixa (1 a 65535)", "port out of range (1 to 65535)", "puerto fuera de rango (1 a 65535)" },
    [GH_T_ROTA_INVALIDA] = { "rota inválida -- ex.: /credencial", "invalid route -- e.g. /credencial", "ruta inválida -- ej.: /credencial" },
    [GH_T_JA_NA_LISTA] = { "essa guilda já está na lista", "that guild is already in the list", "ese gremio ya está en la lista" },
    [GH_T_FALTA_CAMPO] = { "falta preencher: %s", "still empty: %s", "falta llenar: %s" },
    [GH_T_SALVA] = { "guilda salva; sonde para conferir", "guild saved; probe it to check", "gremio guardado; sondéalo para comprobar" },
    [GH_T_NAO_SALVEI] = { "não salvei; os campos continuam aí", "not saved; the fields are still there", "no guardé; los campos siguen ahí" },
    [GH_T_NAO_APAGUEI] = { "não apaguei; a guilda continua aí", "not deleted; the guild is still there", "no borré; el gremio sigue ahí" },
    [GH_T_FICAMOS] = { "continuamos aqui", "staying here", "seguimos aquí" },
    [GH_T_REMOVIDA] = { "guilda removida da lista", "guild removed from the list", "gremio quitado de la lista" },
    [GH_T_REMOVIDAS] = { "%d guildas removidas", "%d guilds removed", "%d gremios quitados" },
    [GH_T_PREENCHA] = { "preencha os campos e toque em Salvar", "fill the fields and tap Save", "rellena los campos y toca Guardar" },
    [GH_T_TOQUE_MUDAR] = { "toque num campo para mudar", "tap a field to change it", "toca un campo para cambiarlo" },
    [GH_T_REDE_RELIDA] = { "rede relida", "network re-read", "red releída" },
    [GH_T_ATE_A_PROXIMA] = { "até a próxima caçada", "see you on the next hunt", "hasta la próxima cacería" },
    [GH_T_SONDANDO] = { "sondando %s (%d de %d)...", "probing %s (%d of %d)...", "sondeando %s (%d de %d)..." },
    [GH_T_CADASTRANDO] = { "cadastrando em %s (%d de %d)...", "signing up at %s (%d of %d)...", "registrando en %s (%d de %d)..." },
    [GH_T_RESUMO_SONDA] = { "%d já cadastrada%s, %d só respondeu, %d sem resposta", "%d already signed up%s, %d answered only, %d silent", "%d ya registrado%s, %d solo respondió, %d sin respuesta" },
    [GH_T_RESUMO_CAD] = { "%d cadastrada%s, %d já tinha, %d falhou", "%d signed up%s, %d already had, %d failed", "%d registrado%s, %d ya tenía, %d falló" },
    [GH_T_NENHUMA_RESP] = { "nenhuma das %d respondeu", "none of the %d answered", "ninguno de los %d respondió" },
    [GH_T_NENHUMA_ACEITOU] = { "nenhuma das %d aceitou o cadastro", "none of the %d accepted the sign-up", "ninguno de los %d aceptó el registro" },
    [GH_T_SEM_MONTAR] = { "não consegui montar o envio", "could not build the request", "no pude armar el envío" },
    [GH_T_SEM_FRD] = { "não consegui abrir o serviço de amigos (frd)", "could not open the friends service (frd)", "no pude abrir el servicio de amigos (frd)" },
    [GH_T_SEM_CRED] = { "o console não devolveu a credencial (0x%08lX / 0x%08lX)", "the console did not return the credential (0x%08lX / 0x%08lX)", "la consola no devolvió la credencial (0x%08lX / 0x%08lX)" },
    [GH_T_SEM_CONTA] = { "este console não tem conta NEX configurada", "this console has no NEX account set up", "esta consola no tiene cuenta NEX configurada" },
    [GH_T_NAO_LIDA] = { "credencial ainda não lida", "credential not read yet", "credencial aún no leída" },
    [GH_T_APAGAR_ESCOLHA] = { "escolha uma guilda na lista para apagar", "pick a guild in the list to delete", "elige un gremio de la lista para borrar" },
    [GH_T_NOME_PADRAO] = { "Guilda do DNS atual", "Guild at current DNS", "Gremio del DNS actual" },
    [GH_T_P_NOME] = { "Nome da guilda", "Guild name", "Nombre del gremio" },
    [GH_T_P_IP] = { "Endereço IP do servidor", "Server IP address", "Dirección IP del servidor" },
    [GH_T_P_PORTA] = { "Porta do cadastro (não é a do DNS)", "Sign-up port (not the DNS one)", "Puerto del registro (no el del DNS)" },
    [GH_T_P_ROTA] = { "Rota de cadastro", "Sign-up route", "Ruta de registro" },
    [GH_T_SAIU_SEM_SALVAR] = { "saí sem salvar", "left without saving", "salí sin guardar" },
};

const char *gh_texto(int id, int idioma)
{
    if (id < 0 || id >= GH_T_TOTAL) return "";
    if (idioma < 0 || idioma >= GH_N_IDIOMAS) idioma = GH_EN;
    const char *t = TEXTOS[id][idioma];
    /* Coluna vazia cai no inglês em vez de sumir da tela. */
    return (t && t[0]) ? t : TEXTOS[id][GH_EN];
}

const char *gh_resultado_texto_i(GhResultado r, int idioma)
{
    switch (r) {
    case GH_RES_REDE:       return gh_texto(GH_T_REDE, idioma);
    case GH_RES_ROTA:       return gh_texto(GH_T_ROTA, idioma);
    case GH_RES_RECUSADO:   return gh_texto(GH_T_RECUSADO, idioma);
    case GH_RES_CONFLITO:   return gh_texto(GH_T_CONFLITO, idioma);
    case GH_RES_JA_TINHA:   return gh_texto(GH_T_JA_TINHA, idioma);
    case GH_RES_REGISTRADO: return gh_texto(GH_T_REGISTRADO, idioma);
    case GH_RES_SEM_MONTAR: return gh_texto(GH_T_SEM_MONTAR, idioma);
    }
    return "";
}

const char *gh_dns_rotulo_i(GhDns d, int idioma)
{
    switch (d) {
    case GH_DNS_AUSENTE:   return gh_texto(GH_T_DNS_SEM, idioma);
    case GH_DNS_ROTEADOR:  return gh_texto(GH_T_DNS_AUTO, idioma);
    case GH_DNS_PUBLICO:   return gh_texto(GH_T_DNS_PUB, idioma);
    case GH_DNS_CANDIDATO: return gh_texto(GH_T_DNS_PERS, idioma);
    }
    return "";
}

const char *gh_dns_explica_i(GhDns d, int idioma)
{
    switch (d) {
    case GH_DNS_AUSENTE:   return gh_texto(GH_T_DNS_SEM_X, idioma);
    case GH_DNS_ROTEADOR:  return gh_texto(GH_T_DNS_AUTO_X, idioma);
    case GH_DNS_PUBLICO:   return gh_texto(GH_T_DNS_PUB_X, idioma);
    case GH_DNS_CANDIDATO: return gh_texto(GH_T_DNS_PERS_X, idioma);
    }
    return "";
}

const char *gh_resultado_texto(GhResultado r)
{
    switch (r) {
    case GH_RES_REDE:       return "não consegui falar com o servidor";
    case GH_RES_SEM_MONTAR: return "não consegui montar o envio";
    case GH_RES_ROTA:       return "essa rota não aceita cadastro";
    case GH_RES_RECUSADO:   return "o servidor recusou o envio";
    case GH_RES_CONFLITO:   return "este console já tem outra senha lá";
    case GH_RES_JA_TINHA:   return "já estava cadastrado; nada mudou";
    case GH_RES_REGISTRADO: return "cadastro feito, pode entrar";
    }
    return "?";
}

/* ------------------------------------------------------------ perfis --- */

/* Numero de campo do arquivo de perfis. Devolve -1 no que nao for numero --
   assim uma linha corrompida cai fora em vez de virar porta 0. */
static int atoi_seguro(const char *s)
{
    int v = 0, d = 0;
    for (; *s >= '0' && *s <= '9'; s++) {
        v = v * 10 + (*s - '0');
        if (++d > 6) return -1;
    }
    return d ? v : -1;
}

/* Copia [ini,fim) truncando. Devolve 0 se coube inteiro, -1 se sobrou. */
static int campo(char *dst, size_t n, const char *ini, const char *fim)
{
    size_t tam = (size_t)(fim - ini);
    int coube = tam < n;
    if (!coube) tam = n - 1;
    memcpy(dst, ini, tam);
    dst[tam] = '\0';
    return coube ? 0 : -1;
}

int gh_perfis_ler(const char *texto, GhPerfil *saida, int max)
{
    if (!texto || !saida || max <= 0) return 0;
    int qtd = 0;

    for (const char *linha = texto; *linha && qtd < max; ) {
        const char *fim = strchr(linha, '\n');
        if (!fim) fim = linha + strlen(linha);

        /* pula vazia e comentario */
        if (fim > linha && *linha != '#') {
            const char *campos[5];
            int n = 0;
            campos[n++] = linha;
            for (const char *p = linha; p < fim && n < 5; p++)
                if (*p == '\t') campos[n++] = p + 1;

            if (n == 5) {
                GhPerfil p;
                memset(&p, 0, sizeof p);
                /* o nome pode ser truncado; ip e rota nao -- truncar ali
                   mudaria para onde a senha vai */
                campo(p.nome, sizeof p.nome, campos[0], campos[1] - 1);
                int bom = campo(p.ip,   sizeof p.ip,   campos[1], campos[2] - 1) == 0
                       && campo(p.rota, sizeof p.rota, campos[3], campos[4] - 1) == 0;

                char porta[12], estado[12];
                bom = bom
                   && campo(porta,  sizeof porta,  campos[2], campos[3] - 1) == 0
                   && campo(estado, sizeof estado, campos[4], fim) == 0;

                if (bom) {
                    p.porta  = atoi_seguro(porta);
                    p.estado = atoi_seguro(estado);
                    if (p.estado < GH_PERFIL_NOVO || p.estado > GH_PERFIL_FALHOU)
                        p.estado = GH_PERFIL_NOVO;
                    if (p.nome[0] && gh_ip_valido(p.ip) && gh_rota_valida(p.rota)
                        && p.porta >= 1 && p.porta <= 65535)
                        saida[qtd++] = p;
                }
            }
        }
        linha = (*fim == '\n') ? fim + 1 : fim;
    }
    return qtd;
}

int gh_perfis_escrever(char *saida, size_t n, const GhPerfil *perfis, int qtd)
{
    if (!saida || n == 0 || !perfis) return -1;
    int o = snprintf(saida, n, "# guild-hunter perfis v1\n"
                               "# nome\tip\tporta\trota\testado\n");
    if (o < 0 || (size_t)o >= n) { saida[0] = '\0'; return -1; }

    for (int i = 0; i < qtd; i++) {
        int escrito = snprintf(saida + o, n - o, "%s\t%s\t%d\t%s\t%d\n",
                               perfis[i].nome, perfis[i].ip, perfis[i].porta,
                               perfis[i].rota, perfis[i].estado);
        if (escrito < 0 || (size_t)escrito >= n - o) { saida[0] = '\0'; return -1; }
        o += escrito;
    }
    return o;
}

int gh_perfil_achar(const GhPerfil *perfis, int qtd, const char *ip, int porta,
                    const char *rota)
{
    if (!perfis || !ip || !rota) return -1;
    for (int i = 0; i < qtd; i++)
        if (perfis[i].porta == porta && strcmp(perfis[i].ip, ip) == 0
            && strcmp(perfis[i].rota, rota) == 0)
            return i;
    return -1;
}


/* ------------------------------------------------------------------ som --

   A familia sonora e a do balcao da guilda: madeira seca, nada eletronico.
   Cada som e um ataque curto de ruido (a batida) somado a um corpo com
   harmonicos e queda exponencial (a ressonancia da madeira).
*/

/* Ruido proprio, com semente fixa: precisa sair identico no PC e no console,
   senao o arquivo que a gente escuta nao e o que o console toca. */
static unsigned _semente;
static float ruido(void)
{
    _semente = _semente * 1103515245u + 12345u;
    return (float)((_semente >> 9) & 0xFFFF) / 32768.0f - 1.0f;
}

static float queda(float t, float k) { return expf(-t * k); }

/* batida de madeira: o transiente que da o carater percussivo */
static float batida(float t, float forca, float k)
{
    return ruido() * forca * queda(t, k);
}

const char *gh_som_nome(int som)
{
    switch (som) {
    case GH_SOM_MOVER:     return "select-menu-item";
    case GH_SOM_CONFIRMA:  return "confirm-menu-item";
    case GH_SOM_SALVAR:    return "app-select-sound";
    case GH_SOM_EDITAR:    return "open-guild-to-edit";
    case GH_SOM_ACHOU:     return "register-found";
    case GH_SOM_NAO_ACHOU: return "no-register-found";
    case GH_SOM_SUCESSO:   return "quest_depart";
    case GH_SOM_ERRO:      return "error";
    case GH_SOM_SAIR:      return "exit";
    }
    return NULL;
}

static float duracao_de(int som)
{
    switch (som) {
    case GH_SOM_MOVER:     return 0.035f;
    case GH_SOM_CONFIRMA:  return 0.130f;
    case GH_SOM_SALVAR:    return 0.340f;
    case GH_SOM_EDITAR:    return 0.110f;
    case GH_SOM_ACHOU:     return 0.240f;
    case GH_SOM_NAO_ACHOU: return 0.240f;
    case GH_SOM_SUCESSO:   return 0.460f;
    case GH_SOM_ERRO:      return 0.220f;
    case GH_SOM_SAIR:      return 0.180f;
    }
    return 0.0f;
}

#define TAU 6.2831853f

size_t gh_sintetizar(int som, short *saida, size_t max, unsigned taxa)
{
    if (!saida || !gh_som_nome(som) || taxa < 8000) return 0;
    float dur = duracao_de(som);
    size_t n = (size_t)(dur * taxa);
    if (n == 0 || n > max) return 0;

    _semente = 0x9B2E2E00u + (unsigned)som;   /* mesma semente, mesmo audio */

    for (size_t i = 0; i < n; i++) {
        float t = (float)i / (float)taxa;
        float v = 0.0f;

        switch (som) {
        case GH_SOM_MOVER:
            v = sinf(TAU * 1400.0f * t) * 0.30f * queda(t, 110.0f)
              + batida(t, 0.22f, 420.0f);
            break;

        case GH_SOM_CONFIRMA:
            /* duas vozes em quinta: soa resolvido sem virar melodia */
            v = sinf(TAU * 700.0f * t) * 0.26f * queda(t, 30.0f)
              + sinf(TAU * 1050.0f * t) * 0.16f * queda(t, 42.0f)
              + batida(t, 0.20f, 300.0f);
            break;

        case GH_SOM_ERRO:
            /* dois graves quase iguais: o batimento da o incomodo */
            v = sinf(TAU * 196.0f * t) * 0.30f * queda(t, 13.0f)
              + sinf(TAU * 185.0f * t) * 0.24f * queda(t, 13.0f)
              + batida(t, 0.14f, 160.0f);
            break;

        case GH_SOM_SUCESSO: {
            /* tres notas subindo, como missao aceita no balcao */
            static const float notas[3] = { 523.25f, 659.25f, 783.99f };
            float passo = dur / 3.0f;
            int k = (int)(t / passo);
            if (k > 2) k = 2;
            float tn = t - k * passo;
            v = sinf(TAU * notas[k] * tn) * 0.26f * queda(tn, 16.0f)
              + sinf(TAU * notas[k] * 2.0f * tn) * 0.10f * queda(tn, 26.0f)
              + batida(tn, 0.16f, 300.0f);
            break;
        }

        case GH_SOM_SALVAR: {
            /* duas notas em quarta, com corpo: algo foi firmado */
            float f = (t < dur * 0.45f) ? 392.0f : 523.25f;
            float tn = (t < dur * 0.45f) ? t : t - dur * 0.45f;
            v = sinf(TAU * f * tn) * 0.24f * queda(tn, 9.0f)
              + sinf(TAU * f * 2.0f * tn) * 0.09f * queda(tn, 16.0f)
              + batida(tn, 0.14f, 240.0f);
            break;
        }

        case GH_SOM_EDITAR:
            /* mais claro que o de mover: algo abriu, nao so andou */
            v = sinf(TAU * 1050.0f * t) * 0.24f * queda(t, 40.0f)
              + sinf(TAU * 1575.0f * t) * 0.10f * queda(t, 55.0f)
              + batida(t, 0.18f, 330.0f);
            break;

        case GH_SOM_ACHOU:
            /* sobe: o que a gente procurava estava la */
            v = sinf(TAU * (587.33f + 260.0f * (t / dur)) * t) * 0.24f * queda(t, 12.0f)
              + batida(t, 0.14f, 280.0f);
            break;

        case GH_SOM_NAO_ACHOU:
            /* desce, mas sem aspereza: nao achar nao e erro */
            v = sinf(TAU * (587.33f - 180.0f * (t / dur)) * t) * 0.22f * queda(t, 12.0f)
              + batida(t, 0.12f, 280.0f);
            break;

        case GH_SOM_SAIR: {
            /* varredura descendo: fecha o assunto */
            float f = 760.0f - 420.0f * (t / dur);
            v = sinf(TAU * f * t) * 0.26f * queda(t, 18.0f)
              + batida(t, 0.12f, 260.0f);
            break;
        }
        }

        /* Rampa nas duas pontas. Sem ela o buffer comeca e termina no meio da
           onda, e cada toque vira um estalo no alto-falante. */
        float borda = 0.004f;
        if (t < borda)       v *= t / borda;
        if (dur - t < borda) v *= (dur - t) / borda;

        if (v > 0.95f)  v = 0.95f;
        if (v < -0.95f) v = -0.95f;
        saida[i] = (short)(v * 32767.0f);
    }
    return n;
}

/* De quem cada som pega emprestado quando nao tem arquivo proprio. A escolha
   e por PARENTESCO de significado: erro puxa "nao achei", que tambem e um
   desfecho negativo; sair puxa o de mover, que e o som neutro da casa. */
static const int PARENTE[GH_SOM_TOTAL] = {
    [GH_SOM_MOVER]     = GH_SOM_CONFIRMA,
    [GH_SOM_CONFIRMA]  = GH_SOM_MOVER,
    [GH_SOM_SALVAR]    = GH_SOM_CONFIRMA,
    [GH_SOM_EDITAR]    = GH_SOM_CONFIRMA,
    [GH_SOM_ACHOU]     = GH_SOM_CONFIRMA,
    [GH_SOM_NAO_ACHOU] = GH_SOM_ERRO,
    [GH_SOM_SUCESSO]   = GH_SOM_CONFIRMA,
    [GH_SOM_ERRO]      = GH_SOM_NAO_ACHOU,
    [GH_SOM_SAIR]      = GH_SOM_MOVER,
};

int gh_som_alternativa(int som, const unsigned char *tem)
{
    if (!tem || som < 0 || som >= GH_SOM_TOTAL) return -1;
    if (tem[som]) return som;

    /* A cadeia tem ciclo de proposito (erro <-> nao-achei): andar um numero
       fixo de passos e parar e mais simples e mais seguro que desenhar uma
       arvore sem volta que alguem quebra na proxima edicao. */
    int i = som;
    for (int passo = 0; passo < GH_SOM_TOTAL; passo++) {
        i = PARENTE[i];
        if (tem[i]) return i;
    }
    /* Ninguem do parentesco tem arquivo: procura qualquer um que tenha. */
    for (i = 0; i < GH_SOM_TOTAL; i++)
        if (tem[i]) return i;
    return -1;
}

/* Um .wav e uma sequencia de blocos; o que importa e o "fmt " (formato) e o
   "data" (as amostras). Ler os dois na ordem em que aparecem e mais seguro do
   que assumir o cabecalho de 44 bytes que a maioria dos arquivos tem -- muitos
   gravadores enfiam blocos extras antes. */
static unsigned le32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8)
         | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
static unsigned le16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

/* Varre os blocos. `declarado` sai com o tamanho que o bloco data ANUNCIA --
   conferir se ele cabe e responsabilidade de quem chamou, porque depende de
   ter o arquivo todo em maos ou nao. */
static int wav_varrer(const unsigned char *d, size_t n, size_t *inicio,
                      size_t *declarado, unsigned *taxa)
{
    if (!d || n < 44) return 0;
    if (memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) return 0;

    int achou_fmt = 0;
    size_t o = 12;
    while (o + 8 <= n) {
        unsigned tam = le32(d + o + 4);
        size_t corpo = o + 8;

        if (memcmp(d + o, "data", 4) == 0) {
            if (!achou_fmt) return 0;       /* data antes de fmt: recusa */
            *inicio = corpo;
            *declarado = tam;
            return 1;
        }

        /* Bloco que nao e o data precisa caber para dar para pular. */
        if (tam > n - corpo) return 0;

        if (memcmp(d + o, "fmt ", 4) == 0) {
            if (tam < 16) return 0;
            if (le16(d + corpo) != 1) return 0;              /* PCM puro */
            if (le16(d + corpo + 2) != 1) return 0;          /* mono */
            if (le16(d + corpo + 14) != 16) return 0;        /* 16 bits */
            *taxa = le32(d + corpo + 4);
            if (*taxa < 8000 || *taxa > 48000) return 0;
            achou_fmt = 1;
        }
        o = corpo + tam + (tam & 1);        /* blocos alinham em 2 bytes */
    }
    return 0;
}

int gh_wav_cabecalho(const unsigned char *d, size_t n, size_t *inicio,
                     unsigned *taxa)
{
    size_t declarado = 0;
    if (!inicio || !taxa) return 0;
    return wav_varrer(d, n, inicio, &declarado, taxa);
}

int gh_wav_pcm16(const unsigned char *d, size_t n, size_t *inicio,
                 size_t *amostras, unsigned *taxa)
{
    size_t declarado = 0;
    if (!inicio || !amostras || !taxa) return 0;
    if (!wav_varrer(d, n, inicio, &declarado, taxa)) return 0;
    /* Aqui o arquivo esta inteiro na memoria: se o bloco diz ser maior do que
       o que temos, o arquivo esta cortado ou mentindo. */
    if (declarado > n - *inicio) return 0;
    *amostras = declarado / 2;
    return *amostras > 0;
}


/* ---------------------------------------------------------- ADPCM ------ */

static int _limitar(int v)
{
    return v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
}

static int _amostra(int nibble, int escala, int c1, int c2, int ant1, int ant2)
{
    return _limitar((((nibble << escala) * 2048 + 1024
                      + c1 * ant1 + c2 * ant2) >> 11));
}

/* Ajuste de ordem 2 sobre um trecho: acha (a1, a2) que melhor preveem. */
static void _lpc2(const short *a, size_t n, double *s1, double *s2)
{
    double m[2][2] = {{0, 0}, {0, 0}}, b[2] = {0, 0};
    for (size_t i = 2; i < n; i++) {
        double x1 = a[i - 1], x2 = a[i - 2], y = a[i];
        m[0][0] += x1 * x1; m[0][1] += x1 * x2;
        m[1][0] += x1 * x2; m[1][1] += x2 * x2;
        b[0] += x1 * y;     b[1] += x2 * y;
    }
    double det = m[0][0] * m[1][1] - m[0][1] * m[1][0];
    if (det > -1e-6 && det < 1e-6) { *s1 = *s2 = 0; return; }
    *s1 = (b[0] * m[1][1] - b[1] * m[0][1]) / det;
    *s2 = (m[0][0] * b[1] - m[1][0] * b[0]) / det;
}

void gh_adpcm_filtros(const short *a, size_t n, short *saida)
{
    /* Um filtro só serviria para som parado; ataque e cauda pedem previsões
       diferentes. Amostra trechos espalhados pela música e agrupa. */
    double p1[256], p2[256];
    int qtd = 0;
    size_t passo = n / 256 > 448 ? n / 256 : 448;
    for (size_t o = 0; o + 448 <= n && qtd < 256; o += passo) {
        double s1, s2;
        _lpc2(a + o, 448, &s1, &s2);
        if (s1 > -4 && s1 < 4 && s2 > -4 && s2 < 4) {
            p1[qtd] = s1; p2[qtd] = s2; qtd++;
        }
    }
    if (qtd == 0) {
        for (int i = 0; i < GH_ADPCM_FILTROS * 2; i++) saida[i] = 0;
        return;
    }

    double c1[GH_ADPCM_FILTROS], c2[GH_ADPCM_FILTROS];
    for (int k = 0; k < GH_ADPCM_FILTROS; k++) {
        int i = (qtd - 1) * k / (GH_ADPCM_FILTROS - 1);
        c1[k] = p1[i]; c2[k] = p2[i];
    }
    for (int volta = 0; volta < 8; volta++) {
        double s1[GH_ADPCM_FILTROS] = {0}, s2[GH_ADPCM_FILTROS] = {0};
        int cont[GH_ADPCM_FILTROS] = {0};
        for (int i = 0; i < qtd; i++) {
            int melhor = 0;
            double dm = 1e18;
            for (int k = 0; k < GH_ADPCM_FILTROS; k++) {
                double d = (p1[i] - c1[k]) * (p1[i] - c1[k])
                         + (p2[i] - c2[k]) * (p2[i] - c2[k]);
                if (d < dm) { dm = d; melhor = k; }
            }
            s1[melhor] += p1[i]; s2[melhor] += p2[i]; cont[melhor]++;
        }
        for (int k = 0; k < GH_ADPCM_FILTROS; k++)
            if (cont[k]) { c1[k] = s1[k] / cont[k]; c2[k] = s2[k] / cont[k]; }
    }
    for (int k = 0; k < GH_ADPCM_FILTROS; k++) {
        int v1 = (int)(c1[k] * 2048 + (c1[k] >= 0 ? 0.5 : -0.5));
        int v2 = (int)(c2[k] * 2048 + (c2[k] >= 0 ? 0.5 : -0.5));
        saida[k * 2]     = (short)_limitar(v1);
        saida[k * 2 + 1] = (short)_limitar(v2);
    }
}

size_t gh_adpcm_codificar(const short *a, size_t n, const short *filtros,
                          unsigned char *saida)
{
    size_t escrito = 0;
    int ant1 = 0, ant2 = 0;

    for (size_t i = 0; i < n; i += GH_ADPCM_QUADRO) {
        int bloco[GH_ADPCM_QUADRO];
        int quantas = 0;
        for (int k = 0; k < GH_ADPCM_QUADRO && i + k < n; k++)
            bloco[quantas++] = a[i + k];
        for (int k = quantas; k < GH_ADPCM_QUADRO; k++) bloco[k] = 0;

        long melhor_erro = -1;
        int melhor_p = 0, melhor_e = 0, melhor_n[GH_ADPCM_QUADRO];
        int melhor_a1 = ant1, melhor_a2 = ant2;

        for (int p = 0; p < GH_ADPCM_FILTROS; p++) {
            int c1 = filtros[p * 2], c2 = filtros[p * 2 + 1];

            /* Estima a escala pelo resíduo em malha aberta e tenta só a
               vizinhança: varrer as 16 dá o mesmo por 5x o tempo. */
            int a1 = ant1, a2 = ant2, pior = 0;
            for (int k = 0; k < GH_ADPCM_QUADRO; k++) {
                int r = bloco[k] - ((c1 * a1 + c2 * a2) >> 11);
                if (r < 0) r = -r;
                if (r > pior) pior = r;
                a2 = a1; a1 = bloco[k];
            }
            int e0 = 0;
            while (e0 < 15 && (7 << e0) < pior) e0++;
            int ini = e0 > 0 ? e0 - 1 : 0;
            int fim = e0 + 3 < 16 ? e0 + 3 : 16;

            for (int escala = ini; escala < fim; escala++) {
                a1 = ant1; a2 = ant2;
                long erro = 0;
                int nib[GH_ADPCM_QUADRO];
                for (int k = 0; k < GH_ADPCM_QUADRO; k++) {
                    int pred = c1 * a1 + c2 * a2;
                    long alvo = ((long)bloco[k] << 11) - pred - 1024;
                    long div = 2048L << escala;
                    long ideal = (alvo >= 0) ? (alvo + div / 2) / div
                                             : (alvo - div / 2) / div;
                    int nb = (int)(ideal < -8 ? -8 : (ideal > 7 ? 7 : ideal));
                    int v = _amostra(nb, escala, c1, c2, a1, a2);
                    long d = v - bloco[k];
                    erro += d * d;
                    nib[k] = nb & 0xF;
                    a2 = a1; a1 = v;
                }
                if (melhor_erro < 0 || erro < melhor_erro) {
                    melhor_erro = erro; melhor_p = p; melhor_e = escala;
                    melhor_a1 = a1; melhor_a2 = a2;
                    for (int k = 0; k < GH_ADPCM_QUADRO; k++) melhor_n[k] = nib[k];
                }
                if (erro == 0) break;
            }
            if (melhor_erro == 0) break;
        }

        saida[escrito++] = (unsigned char)((melhor_p << 4) | melhor_e);
        for (int k = 0; k < GH_ADPCM_QUADRO; k += 2)
            saida[escrito++] = (unsigned char)((melhor_n[k] << 4) | melhor_n[k + 1]);
        ant1 = melhor_a1; ant2 = melhor_a2;
    }
    return escrito;
}

size_t gh_adpcm_decodificar(const unsigned char *d, size_t bytes,
                            const short *filtros, short *saida, size_t max,
                            short *ant1, short *ant2)
{
    size_t fora = 0;
    int a1 = ant1 ? *ant1 : 0, a2 = ant2 ? *ant2 : 0;

    for (size_t i = 0; i + 8 <= bytes; i += 8) {
        int cab = d[i];
        int p = (cab >> 4) & 7, escala = cab & 0xF;
        int c1 = filtros[p * 2], c2 = filtros[p * 2 + 1];
        for (int k = 0; k < GH_ADPCM_QUADRO; k++) {
            int b = d[i + 1 + k / 2];
            int nb = (k % 2 == 0) ? (b >> 4) : (b & 0xF);
            if (nb > 7) nb -= 16;
            int v = _amostra(nb, escala, c1, c2, a1, a2);
            if (fora < max) saida[fora++] = (short)v;
            a2 = a1; a1 = v;
        }
    }
    if (ant1) *ant1 = (short)a1;
    if (ant2) *ant2 = (short)a2;
    return fora;
}
