# Guild Hunter

App homebrew de **Nintendo 3DS** que registra a credencial NEX do console num
servidor privado de Monster Hunter — sem apagar a conta nem enviar dump de save.
O console lê a própria senha (`FRD_GetMyPassword`) e a manda para o servidor.

Sai em **`.3dsx`** (roda pelo Homebrew Launcher) e **`.cia`** (instala no menu
HOME pelo FBI).

## Nada da Capcom aqui

O código é próprio, e os sons do build distribuído são **sintetizados** — a
família sonora do balcão da guilda, calculada em `source/nucleo.c` (madeira seca,
nada gravado). Quem tem o jogo pode pôr as próprias gravações em `sons/` e
recompilar; elas ficam na máquina de quem gravou e **não** entram neste
repositório (`sons/`, `romfs/` e a arte própria são gitignored).

## Latest build

O build mais recente (sons sintetizados, sem conteúdo da Capcom) fica em `dist/`:

- `dist/guild-hunter.cia` — instala pelo **FBI** (menu HOME), ou pelo
  **FBI → Remote Install** apontando para a URL do `.cia`
- `dist/guild-hunter.3dsx` — copie para `sdmc:/3ds/` e abra pelo **Homebrew Launcher**

## Compilar

Precisa de **Docker** (usa a imagem oficial `devkitpro/devkitarm`, então não é
preciso instalar o toolchain à mão):

```bash
./build.sh          # gera o .3dsx
./build.sh cia      # gera o .3dsx + o .cia (FBI)
./build.sh clean    # limpa
```

Sem a pasta `sons/`, o build usa o áudio sintetizado. Com `sons/*.mp3` (gravações
próprias), ele embute as suas.

## Como funciona (curto)

Toda decisão mora em `source/nucleo.c` — C puro, sem cabeçalho de 3DS, testável
no PC. O `source/main.c` desenha e fala com os serviços do console; o
`source/som.c` toca o áudio (a música reabastece numa thread própria, fora do
laço de desenho). Armadilhas de 3DS e detalhes em `LEIA-ME.md`.
