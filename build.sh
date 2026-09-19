#!/usr/bin/env bash
# Compila o Guild Hunter usando a imagem oficial da devkitPro.
#
# Por que num contêiner: o repositório da devkitPro (apt.devkitpro.org) está
# atrás de um desafio da Cloudflare que devolve 403 para qualquer coisa que
# não seja um navegador, então não dá para instalar o toolchain por linha de
# comando. A imagem que eles publicam no Docker Hub tem tudo -- devkitARM,
# libctru, citro2d, 3dsxtool -- e não depende daquele repositório.
#
#     ./homebrew/guild-hunter/build.sh          compila o .3dsx
#     ./homebrew/guild-hunter/build.sh cia      .3dsx + .cia (para o FBI)
#     ./homebrew/guild-hunter/build.sh clean    limpa
#
# Sai em homebrew/guild-hunter/guild-hunter.3dsx -- copie para a pasta
# /3ds/ do cartão SD do console.
set -euo pipefail

AQUI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGEM="devkitpro/devkitarm:latest"
ALVO="${1:-all}"
FAZER_CIA=0
[[ "$ALVO" == "cia" ]] && { ALVO=all; FAZER_CIA=1; }

FERRAMENTAS="$(cd "$AQUI/.." && pwd)/ferramentas"

# Identificador único do título. Fica fora das faixas da Nintendo, na região
# que o homebrew usa. Não invente outro: mudar isto depois faz o console
# tratar como um app diferente, e a pessoa acaba com dois ícones.
ID_UNICO=0xff3aa
CODIGO_PRODUTO="CTR-P-GHUN"

docker() {
  # Logo depois de instalar o docker.io a sessão ainda não tem o grupo novo;
  # o `sg` resolve sem exigir que a pessoa saia e entre de novo.
  if command docker info >/dev/null 2>&1; then command docker "$@"
  else sg docker -c "docker $(printf '%q ' "$@")"; fi
}

if ! command -v docker >/dev/null 2>&1; then
  echo "docker não encontrado. Instale com:  sudo apt install docker.io" >&2
  exit 1
fi

# --- audio embutido -------------------------------------------------------
# Ponha os seus arquivos em homebrew/guild-hunter/sons/ (mp3, wav, o que for)
# e eles vao DENTRO do .3dsx -- sem precisar copiar pasta nenhuma para o
# cartao depois. A pasta e o romfs/ gerado sao ignorados pelo git de proposito:
# o audio e de quem montou o build.
#
# A conversao e a tabela de nomes ficam no preparar-sons.sh, um lugar so. Ter
# uma segunda copia aqui seria ter uma que ninguem roda e que um dia diverge.
preparar_audio() {
  saida="$AQUI/build"
  mkdir -p "$saida" "$AQUI/romfs/sons"
  cat > "$AQUI/romfs/LEIA-ME.txt" <<'TXT'
Guild Hunter -- conteudo embutido no build.
Os .wav em sons/ vieram da pasta sons/ de quem compilou este arquivo.
TXT

  if [[ -d "$AQUI/sons" ]] && compgen -G "$AQUI/sons/*" >/dev/null; then
    "$AQUI/preparar-sons.sh" "$AQUI/sons" "$AQUI/romfs/sons"

    # A trilha vai em ADPCM, não em PCM: 4x menor no arquivo E 4x menos
    # leitura de cartão por bloco, que é o que fazia a música engasgar.
    if [[ -f "$AQUI/romfs/sons/musica.wav" ]]; then
      cc -I "$AQUI" -O2 -o "$saida/gerar_musica" "$AQUI/gerar_musica.c" \
         "$AQUI/source/nucleo.c" -lm 2>/dev/null \
        && "$saida/gerar_musica" "$AQUI/romfs/sons/musica.wav" \
                                 "$AQUI/romfs/sons/musica.gha" \
        && rm -f "$AQUI/romfs/sons/musica.wav"
    fi
  else
    echo "  (sem audio proprio em sons/; o app usa a sintese)"
  fi

  # O make nao olha DENTRO do romfs: com o .3dsx ja existente ele diz "up to
  # date" e entrega o binario antigo, sem os arquivos novos. Isso e pior do
  # que falhar, porque parece que funcionou. Apagar o alvo custa um religamento
  # (segundos, o .elf continua em cache) e garante que o que sai e o que esta
  # na pasta agora.
  rm -f "$AQUI/guild-hunter.3dsx"
}

[[ "$ALVO" == "all" ]] && preparar_audio

docker run --rm \
  -v "$AQUI:/projeto" -w /projeto \
  -u "$(id -u):$(id -g)" \
  "$IMAGEM" make "$ALVO"

if [[ "$ALVO" == "all" ]]; then
  echo
  echo "pronto: $AQUI/guild-hunter.3dsx"
  ls -la "$AQUI"/guild-hunter.3dsx "$AQUI"/guild-hunter.smdh 2>/dev/null || true
fi

# ---------------------------------------------------------------- .cia -----
# O .3dsx roda pelo Homebrew Launcher e não precisa de nada disto. O .cia
# existe para instalar com o FBI e aparecer no menu HOME como um jogo.
#
# Duas ferramentas de fora, que a imagem da devkitPro não traz:
#   makerom     (3DSGuy/Project_CTR)   monta o .cia
#   bannertool  (carstene1ns)          faz o banner e o ícone
# Ficam em homebrew/ferramentas/, ignoradas pelo git -- são binários de
# terceiros, não código nosso.
if [[ "$FAZER_CIA" == "1" ]]; then
  echo
  for f in makerom bannertool; do
    [[ -x "$FERRAMENTAS/$f" ]] || {
      echo "falta $FERRAMENTAS/$f -- veja o LEIA-ME (seção Instalando como CIA)" >&2
      exit 1
    }
  done

  mkdir -p "$saida"

  # Áudio do banner: é o som que o menu HOME toca ao SELECIONAR o aplicativo.
  # É o lugar certo do app-select-sound -- dentro do app não havia como tocá-lo
  # naquele instante, porque nesse momento o nosso código ainda não roda.
  #
  # Usa o seu arquivo se houver; senão, um som sintetizado pelo próprio app.
  audio_banner="$AQUI/romfs/sons/app-select-sound.wav"
  if [[ ! -f "$audio_banner" ]]; then
    cc -I "$AQUI/source" -o "$saida/ouvir" "$AQUI/ouvir.c" \
       "$AQUI/source/nucleo.c" -lm 2>/dev/null \
      && (cd "$saida" && ./ouvir . >/dev/null) \
      && audio_banner="$saida/quest_depart.wav"
    echo "  banner: sem app-select-sound.wav, usando som sintetizado"
  else
    echo "  banner: áudio de $(basename "$audio_banner")"
  fi

  # Visual do banner: o menu HOME mostra uma CENA 3D. Com um .cgfx ele gira e
  # tem profundidade; com um .png fica uma placa chapada, que funciona mas é
  # visivelmente mais pobre. Ponha banner.cgfx aqui e ele é usado sozinho.
  # Ordem: cena 3D pronta > imagem sua > a nossa arte padrão. A "imagem sua"
  # costuma ser um modelo renderizado por renderizar_banner.py, e fica fora do
  # git porque o modelo é de quem montou o build.
  if [[ -f "$AQUI/banner.cgfx" ]]; then
    visual_banner=(-ci "$AQUI/banner.cgfx")
    echo "  banner: cena 3D (banner.cgfx)"
  elif [[ -f "$AQUI/banner-proprio.png" ]]; then
    visual_banner=(-i "$AQUI/banner-proprio.png")
    echo "  banner: imagem própria (banner-proprio.png)"
  else
    visual_banner=(-i "$AQUI/banner.png")
    echo "  banner: arte padrão (banner.png)"
  fi

  # O bannertool só escreve PCM16 -- ele mesmo avisa: "ADPCM encoding is
  # currently unsupported". E o menu HOME espera DSP-ADPCM: conferido no banner
  # do próprio MHXX, extraído do ExeFS do cartucho. Entregando PCM16, o console
  # lê os bytes como se fossem ADPCM e sai uma sequência de bips.
  #
  # Então o CWAV é feito aqui, e entra pronto pelo -ca.
  python3 "$AQUI/cwav.py" "$audio_banner" -o "$saida/banner.bcwav" || exit 1

  "$FERRAMENTAS/bannertool" makebanner \
    "${visual_banner[@]}" -ca "$saida/banner.bcwav" -o "$saida/banner.bnr" >/dev/null

  "$FERRAMENTAS/bannertool" makesmdh \
    -s "Guild Hunter" \
    -l "Cadastro de cacador nas guildas locais" \
    -p "MHXX Local DLC Service" \
    -i "$AQUI/icone.png" -o "$saida/icone.icn" >/dev/null

  "$FERRAMENTAS/makerom" -f cia -o "$AQUI/guild-hunter.cia" \
    -elf "$AQUI/guild-hunter.elf" -rsf "$AQUI/guild-hunter.rsf" \
    -icon "$saida/icone.icn" -banner "$saida/banner.bnr" \
    -exefslogo -target t \
    -DAPP_TITLE="Guild Hunter" \
    -DAPP_PRODUCT_CODE="$CODIGO_PRODUTO" \
    -DAPP_UNIQUE_ID="$ID_UNICO" \
    -DAPP_ROMFS="$AQUI/romfs"

  echo
  echo "pronto: $AQUI/guild-hunter.cia"
  ls -la "$AQUI/guild-hunter.cia"
fi
