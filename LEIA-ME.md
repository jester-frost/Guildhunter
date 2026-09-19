# Guild Hunter

App de 3DS que inscreve o seu console numa guilda local — um servidor de
Monster Hunter que você ou outra pessoa esteja rodando na rede.

O servidor precisa da **credencial NEX** do console para deixar você entrar.
Até aqui só havia dois jeitos de conseguir isso, e os dois cobram caro de quem
não é dono do servidor: apagar as credenciais (você perde a lista de amigos e o
friend code muda) ou mandar o dump do save de amigos — 8 MB com número de
série, lista de amigos, friend code e a credencial — para um estranho.

Este app é a terceira via. Ele lê a credencial **no próprio console**, manda
para a guilda escolhida pela rede local, e esquece. A senha não aparece na
tela, não vai para o cartão e não entra em log nenhum — nem aqui, nem lá.

## Instalando

**Pelo Homebrew Launcher** — copie `guild-hunter.3dsx` para a pasta `/3ds/` do
cartão SD e abra pelo Homebrew Launcher. Não precisa de mais nada.

**Como CIA, pelo FBI** — copie `guild-hunter.cia` para o cartão e instale com o
FBI. Aparece no menu HOME como um jogo.

```bash
./build.sh cia        # gera o .3dsx e o .cia
```

O `.cia` precisa de duas ferramentas que a imagem da devkitPro não traz. Elas
ficam em `homebrew/ferramentas/`, ignorada pelo git — são binários de terceiros:

```bash
mkdir -p homebrew/ferramentas && cd homebrew/ferramentas
curl -L -o makerom.zip https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.19.0/makerom-v0.19.0-ubuntu_x86_64.zip
curl -L -o bt.tgz https://github.com/carstene1ns/3ds-bannertool/releases/download/1.2.3/bannertool-1.2.3-linux.tar.gz
unzip -q makerom.zip && tar xzf bt.tgz && mv bannertool-*/bannertool .
chmod +x makerom bannertool
```

### O banner

O menu HOME mostra uma **cena 3D** quando o aplicativo é selecionado, com som.

- **Som**: sai de `sons/app-select-sound.wav` (o seu), codificado por
  `cwav.py` em **DSP-ADPCM**. O `bannertool` só escreve PCM16 e avisa disso
  nas próprias strings; o menu HOME espera ADPCM, e recebendo PCM16 ele lê os
  bytes como nibbles — o que sai é uma sequência de bips. A planta do arquivo
  foi copiada do banner do próprio MHXX: dois canais, 32728 Hz, e as amostras
  alinhadas em `DATA+0x20` com 24 bytes de enchimento antes. É o lugar certo desse
  arquivo — dentro do app não havia como tocá-lo no instante da seleção,
  porque nesse momento o nosso código ainda não roda. Sem ele, o build usa um
  som sintetizado pelo próprio app.
- **Visual**: com `banner.cgfx` presente, o banner é o modelo 3D — gira e tem
  profundidade. Sem ele, `banner.png` (256×128) vira uma placa chapada:
  funciona, mas é visivelmente mais pobre.

Um `.cgfx` é o formato de cena do 3DS (modelo, texturas, animação num arquivo
só). É formato da Nintendo, do CTR SDK: as ferramentas abertas que existem
**leem** CGFX, não escrevem. Então não há como converter um `.dae` ou `.obj`
para CGFX aqui — é preciso um `.cgfx` já pronto.

O que dá para fazer sem isso é **renderizar o modelo uma vez** e usar o
resultado como imagem do banner. Continua sendo uma placa plana, mas com o
boneco desenhado nela, iluminado e no ângulo escolhido:

```bash
python3 renderizar_banner.py "/caminho/Poogie 1/Poogie 1.dae" \
        -o banner-proprio.png --giro 145 --inclina 12
```

`renderizar_banner.py` lê COLLADA (posições, normais, UVs, triangulando quads),
rasteriza com z-buffer e textura, e monta a moldura da guilda com o letreiro.
Use `--so-o-boneco` para ver só o modelo enquanto procura o ângulo.

O `banner-proprio.png` vence o `banner.png` padrão no build, e é ignorado pelo
git: o modelo é de quem montou.

O áudio e o modelo que você puser vão **dentro do `.cia`**, que é gitignored —
como todo o resto do áudio, é material de quem monta o build.

### Por que o CIA precisa do `guild-hunter.rsf`

Rodando pelo Homebrew Launcher o app **herda** as permissões do launcher, que
são largas. Instalado como CIA ele só tem o que o `.rsf` declara. Faltando
algo, o app não avisa: ele abre e falha numa função só, ou morre no arranque.

Foi o que aconteceu na primeira montagem — o `ndspInit` escrevia na memória do
DSP, que não estava mapeada, e o processo caía. O `.3dsx` do mesmo código
funcionava. O que resolve está no `.rsf`:

```yaml
  IORegisterMapping:
   - 1ec40000-1ec4ffff      # registradores do DSP
  MemoryMapping:
   - 1ff00000-1ff7ffff      # memória do DSP, leitura e escrita
  FileSystemAccess:
   - DirectSdmc             # perfis.txt e os sons do cartão
```

Mais `frd:u`, `soc:U`, `http:C`, `dsp::DSP`, `ac:u` e `APT:U` (o teclado do
sistema é applet) em `ServiceAccessControl`.

## Usando

A tela de cima é a sua ficha: o ID do caçador, o endereço do console, o
roteador e o DNS em uso. A de baixo é a lista de guildas.

| botão | o que faz |
|---|---|
| **Sondar/consultar** | vê se a rota de cadastro existe ali **e** se você já está inscrito |
| **Cadastrar** | manda a credencial — é o que te deixa entrar |
| **Nova guilda** | abre o formulário em branco |
| **Editar guilda** | abre o mesmo formulário, preenchido |

### O formulário

Os quatro campos ficam na tela e cada um se edita tocando nele — nome,
endereço, porta e rota. **Salvar** fica apagado enquanto falta campo, e campo
preenchido com valor inválido fica carmim no próprio lugar, não numa mensagem
genérica no fim.

Ao tocar em **Salvar** aparece uma pergunta, mostrando `ip:porta/rota` do que
vai ser gravado — vale tanto para guilda nova quanto para edição. Os botões
ficam logo abaixo do último campo e é fácil tocar sem querer; uma pergunta a
mais custa um toque, e desfazer custa reabrir e redigitar tudo.

Dentro do formulário o **B** volta para a lista, não fecha o app.

Toque na tela ou use o direcional. **A** confirma, **X** apaga a guilda
marcada, **Y** relê a rede, **B** sai.

### O ícone de cada guilda

| | |
|---|---|
| losango vazado | ainda não sondada |
| losango dourado | a rota existe, você ainda não se cadastrou |
| losango verde com visto | **você está cadastrado aqui** |
| losango vermelho | a última tentativa falhou |
| anel dourado em volta | é o DNS que o console está usando agora |
| tudo riscado | não há DNS personalizado — nenhuma guilda está ao alcance |

## Perdeu a lista?

A lista mora em `sdmc:/3ds/guild-hunter/perfis.txt`. Se você formatou o cartão,
não precisa se cadastrar de novo em lugar nenhum: adicione a guilda e toque em
**Sondar/consultar**. O servidor responde se aquele console já está inscrito, e
o app remonta o histórico — sem reenviar credencial para ninguém.

## Rota personalizada

A rota padrão é `/credencial`, mas ela é campo da guilda, não constante do
programa. Quem administra um servidor pode mover o cadastro para onde quiser —
por segurança, ou para aguentar volume — e do lado de lá basta
`tools/receber_senha.py --rota /a/rota/nova`.

Uma rota tem de começar com `/` e não pode ter espaço, `//` nem quebra de
linha: qualquer um dos três mandaria o pedido para outro lugar, ou quebraria a
requisição.

## Compilando

O repositório da devkitPro está atrás de um desafio da Cloudflare que devolve
403 para tudo que não é navegador, então o toolchain não instala por linha de
comando. A imagem oficial deles no Docker Hub tem tudo:

```bash
sudo apt install docker.io
./build.sh          # -> guild-hunter.3dsx
./build.sh clean
```

## Como o código está dividido

`source/nucleo.c` tem toda a decisão do programa — classificar o DNS, montar
URL e corpo, ler a resposta, guardar a lista — em C puro, sem nenhum cabeçalho
de 3DS. `../../tests/test_guild_hunter.c` roda isso **no PC**, e a suíte em
Python compila e executa esse teste junto com o resto.

`source/main.c` fica só com desenho, entrada e serviços do console.

A divisão não é arrumação: sem ela, conferir uma mudança exigiria compilar,
copiar para o cartão, abrir o launcher e olhar — lento o bastante para, na
prática, não acontecer.

## O que ainda não foi testado

O núcleo passa em 68 verificações no PC. **A interface nunca rodou num console
de verdade** — foi compilada sem avisos, e só. O que ela faz com o hardware na
frente ainda é para descobrir.

## Som

Os efeitos são **sintetizados**, não gravados: madeira seca, na família do
balcão da guilda. O áudio do Monster Hunter é da Capcom e não entra num
repositório público — e som calculado não precisa de arquivo nenhum, o app
não carrega nada para tocar.

A síntese mora em `source/nucleo.c` justamente para poder ser conferida no PC:

```bash
cc -o /tmp/ouvir homebrew/guild-hunter/ouvir.c \
      homebrew/guild-hunter/source/nucleo.c -lm
/tmp/ouvir /tmp/sons          # gera mover/confirma/erro/sucesso/sair + todos.wav
```

É o **mesmo** `gh_sintetizar()` que roda no console — se fossem dois códigos,
o que você escuta aqui não provaria nada sobre o que o 3DS toca.

### Usando os seus arquivos

Se você tem o jogo e quer o áudio dele, ponha os `.wav` no cartão (PCM 16
bits, **mono**) e eles valem mais que a síntese:

| arquivo | quando toca |
|---|---|
| `select-menu-item` | cursor movendo, guilda escolhida, Sair, Não |
| `confirm-menu-item` | botão, campo do formulário, abrir o diálogo |
| `app-select-sound` | o **Sim** do diálogo — a guilda indo para o disco |
| `open-guild-to-edit` | o formulário de guilda abrindo |
| `register-found` | a consulta achou o cadastro |
| `no-register-found` | a rota existe, mas sem cadastro |
| `quest_depart` | cadastro concluído no servidor |
| `error` | falhou de verdade |
| `exit` | saindo |
| `musica` | trilha de fundo, em loop (vira `musica.gha`, veja abaixo) |

**Você não precisa trazer os nove.** O que faltar pega emprestado do seu
próprio conjunto, pelo parentesco de significado — `error` puxa
`no-register-found`, `exit` puxa `select-menu-item`. Só cai na síntese quem
não trouxe som nenhum: um som calculado no meio de gravações de jogo soa como
defeito, não como padrão.

Para ver o mapa resolvido da sua pasta:

```bash
cc -I source -o /tmp/conferir conferir-sons.c source/nucleo.c -lm
/tmp/conferir /caminho/da/pasta/sons
```

A música toca por streaming em dois blocos que se revezam — 10 MB de PCM não
cabem na memória do console — e dá a volta lendo, sem buraco na emenda.
**SELECT** liga e desliga.

Use o `preparar-sons.sh` em vez de converter à mão: além de converter, ele
**corta o silêncio das pontas** dos efeitos. Isso não é capricho — um arquivo
com 645 ms de silêncio na frente faz a interface parecer lenta: você toca, e o
som vem depois. O defeito está no arquivo, mas quem usa culpa o programa.

```bash
./preparar-sons.sh /caminho/do/cartao/3ds/guild-hunter/sons
```

Para saber se os seus arquivos vão tocar, sem precisar do console:

```bash
cc -I source -o /tmp/conferir conferir-sons.c source/nucleo.c -lm
/tmp/conferir sons/*.wav
```

Ele usa o mesmo parser do app. O motivo mais comum de um som não tocar é o
arquivo ser estéreo — o app só aceita mono, e cai na síntese em silêncio.

Esses arquivos ficam no seu cartão. Nada disso é distribuído aqui.

#### A trilha vai em ADPCM

O `build.sh` converte `musica.wav` em `musica.gha` — o mesmo DSP-ADPCM que o
3DS toca **direto, sem a CPU decodificar**.

Não é só economia de espaço, embora ela seja grande (10,3 MB → 2,95 MB, e o
`.cia` inteiro de 10,7 MB para 3,8 MB). O que pesa mesmo é a **leitura de
cartão**: em PCM cada bloco custava 16 KB para 0,25 s de áudio, no meio do
laço principal. Em ADPCM são 8 KB para 0,45 s — menos da metade da pressão,
com quase o dobro de folga. Música engasgando é quase sempre isso.

O formato leva uma tabela de contexto, 6 bytes por bloco: em ADPCM cada
amostra depende das duas anteriores, e como quem decodifica é o DSP, o app não
teria como saber onde um bloco parou. `gerar_musica.c` calcula isso uma vez,
na conversão.

## Se ficar mudo

A tela diz o que falta: `sdmc:/3ds/dspfirm.cdc`.

A libctru **carrega** a firmware do DSP antes de usá-lo, lendo esse arquivo, e
desiste se não achar. Jogo de varejo não passa por esse caminho — por isso um
jogo toca som no emulador e o homebrew não, o que parece defeito do app e não
é. Num console com CFW, o **DSP1** gera o arquivo uma vez e resolve.

No Azahar dá para ter som também: como ele emula o DSP por software, o
conteúdo do arquivo é ignorado e basta que ele exista. Um arquivo qualquer em
`sdmc/3ds/dspfirm.cdc` destrava o áudio no emulador — **mas nunca copie esse
arquivo de mentira para o cartão real**, onde ele carregaria lixo no DSP.
