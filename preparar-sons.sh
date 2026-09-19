#!/usr/bin/env bash
# Converte uma pasta de áudio nos .wav que o Guild Hunter toca.
#
# O app não decodifica mp3 -- decodificar áudio comprimido no 3DS custa
# biblioteca e CPU que não valem a pena para um bipe de menu. Então a conversão
# acontece aqui, uma vez, e o console lê PCM direto.
#
#     ./preparar-sons.sh <pasta-de-origem> [pasta-de-destino]
#
# Sem destino, converte na própria pasta de origem -- que é o caso de uso do
# cartão SD: aponte para sdmc:/3ds/guild-hunter/sons/ e pronto.
#
# O nome do arquivo escolhe o evento. Estes são os nomes que o app procura:
#
#   select-menu-item     cursor andando na lista
#   confirm-menu-item    A, ou toque num botão
#   app-select-sound     o "Sim" do diálogo de salvar
#   open-guild-to-edit   formulário de guilda abrindo
#   register-found       a consulta achou o cadastro
#   no-register-found    a rota existe, mas sem cadastro
#   quest_depart         cadastro concluído no servidor
#   error                falhou de verdade
#   exit                 saindo
#   musica               trilha de fundo, em loop
#
# Apelidos óbvios também valem (cursor, ok, erro, sair, bgm, trilha, tema...).
# O que não for reconhecido é avisado, nunca convertido no lugar errado.
set -euo pipefail

ORIGEM="${1:?uso: preparar-sons.sh <origem> [destino]}"
DESTINO="${2:-$ORIGEM}"

command -v ffmpeg >/dev/null 2>&1 || {
  echo "ffmpeg não encontrado -- instale com: sudo apt install ffmpeg" >&2
  exit 1
}
mkdir -p "$DESTINO"

# Devolve o nome canônico do evento, ou vazio se não reconhecer.
evento_de() {
  local n
  n="$(echo "$1" | iconv -f utf8 -t ascii//TRANSLIT 2>/dev/null || echo "$1")"
  n="$(echo "$n" | tr '[:upper:]_' '[:lower:]-')"
  case "$n" in
    # o "no-" precisa vir antes: no-register-found contém register-found
    *no-register*|*nao-cadastr*|*sem-cadastr*)  echo no-register-found ;;
    *register-found*|*ja-cadastr*|*achou*)      echo register-found ;;
    *select-menu*|*cursor*|*mover*)             echo select-menu-item ;;
    *confirm*|*ok*|*aceit*)                     echo confirm-menu-item ;;
    *app-select*|*salvar*|*save*)               echo app-select-sound ;;
    *open-guild*|*edit*)                        echo open-guild-to-edit ;;
    *quest*|*depart*|*sucesso*|*partida*)       echo quest_depart ;;
    *error*|*erro*|*falha*|*recus*)             echo error ;;
    *exit*|*sair*|*fecha*|*volta*)              echo exit ;;
    *music*|*bgm*|*trilha*|*hall*|*tema*)       echo musica ;;
    *) echo "" ;;
  esac
}

convertidos=0
shopt -s nullglob
for arquivo in "$ORIGEM"/*; do
  [[ -f "$arquivo" ]] || continue
  base="$(basename "${arquivo%.*}")"
  # não reconverter o que já é saída nossa
  [[ "$arquivo" == *.wav && -n "$(evento_de "$base")" \
     && "$base" == "$(evento_de "$base")" ]] && continue

  evento="$(evento_de "$base")"
  if [[ -z "$evento" ]]; then
    echo "  ignorado (nome não reconhecido): $(basename "$arquivo")" >&2
    continue
  fi

  # Corta o silêncio das pontas. Isto não é capricho: um arquivo com 645 ms de
  # silêncio na frente soa como atraso do programa -- o toque acontece, e o
  # som vem depois. O usuário percebe a interface como lenta, e o defeito está
  # no arquivo.
  #
  # Deixa 10 ms na frente e 50 ms no fim: cortar rente ao primeiro pico faz
  # estalo, e cortar rente ao fim engole a cauda do som.
  if [[ "$evento" == "musica" ]]; then
    filtro=""          # a trilha entra inteira; o silêncio dela é parte dela
  else
    filtro="-af silenceremove=start_periods=1:start_silence=0.01:start_threshold=-45dB:detection=peak,areverse,silenceremove=start_periods=1:start_silence=0.05:start_threshold=-45dB:detection=peak,areverse"
  fi

  ffmpeg -v error -y -i "$arquivo" $filtro -ac 1 -ar 32000 -sample_fmt s16 \
         "$DESTINO/$evento.wav"
  echo "  $(basename "$arquivo")  ->  $evento.wav"
  convertidos=$((convertidos + 1))
done
shopt -u nullglob

echo "$convertidos arquivo(s) prontos em $DESTINO"
