#!/usr/bin/env python3
""" Faz o CWAV do banner em DSP-ADPCM, que é o que o menu HOME toca.

Por que isto existe
-------------------
O banner do menu HOME tem áudio. O `bannertool` sabe montar um CWAV, mas só em
PCM 16 bits -- ele mesmo avisa nas próprias strings: "ADPCM encoding is
currently unsupported and will be converted to 16 bit PCM!".

E o menu HOME espera DSP-ADPCM. Conferido no banner do próprio Monster Hunter
XX, extraído do ExeFS do cartucho: `encoding 2 (DSP-ADPCM), 32728 Hz`. O nosso
saía `encoding 1 (PCM16)` -- e o console lia bytes de PCM como se fossem
ADPCM. Não é silêncio nem ruído: sai uma sequência de bips, que foi exatamente
o que o Marcos ouviu no aparelho.

Então este programa faz o que falta: codifica em DSP-ADPCM e escreve o CWAV
com a mesma estrutura do banner de varejo.

    python3 cwav.py entrada.wav -o saida.bcwav

Depois é só `bannertool makebanner -ca saida.bcwav ...`.

Sobre o codec
-------------
DSP-ADPCM guarda 14 amostras em 8 bytes: um byte de cabeçalho com o preditor
(qual dos 8 filtros) e a escala, mais 7 bytes de nibbles. A decodificação é

    amostra = ((nibble << escala) * 2048 + 1024 + c1*ant1 + c2*ant2) >> 11

Codificar é escolher, para cada quadro, o par (preditor, escala) que erra
menos. Os 8 filtros saem de um ajuste de ordem 2 sobre o próprio áudio,
agrupados por semelhança -- áudio diferente pede filtro diferente.
"""

import argparse
import struct
import sys
import wave

import numpy as np

QUADRO = 14          # amostras por quadro
N_FILTROS = 8


# ------------------------------------------------------------- o codec -----

def _lpc2(bloco, ant1, ant2):
    """ Ajuste de ordem 2: acha (a1, a2) que melhor prevê o bloco. """
    x = np.concatenate(([ant2, ant1], bloco)).astype(np.float64)
    n = len(bloco)
    if n < 3:
        return 0.0, 0.0
    X = np.stack([x[1:1 + n], x[0:n]], axis=1)   # ant1, ant2
    y = x[2:2 + n]
    G = X.T @ X
    b = X.T @ y
    G += np.eye(2) * 1e-6
    try:
        a = np.linalg.solve(G, b)
    except np.linalg.LinAlgError:
        return 0.0, 0.0
    return float(a[0]), float(a[1])


def gerar_filtros(amostras):
    """ Oito filtros que cubram o material.

    Um filtro só serviria para som parado; os quadros de ataque e os de cauda
    pedem previsões diferentes. Faz o ajuste quadro a quadro e agrupa os
    resultados -- é o k-médias mais simples possível, e basta: o codificador
    ainda escolhe o melhor dos oito em cada quadro.
    """
    pares = []
    ant1 = ant2 = 0
    for i in range(0, len(amostras) - QUADRO, QUADRO):
        bloco = amostras[i:i + QUADRO]
        a1, a2 = _lpc2(bloco, ant1, ant2)
        if abs(a1) < 4 and abs(a2) < 4:
            pares.append((a1, a2))
        ant2, ant1 = bloco[-2], bloco[-1]

    if not pares:
        return [(0, 0)] * N_FILTROS

    P = np.array(pares)
    # semeia com quantis, para os grupos nascerem espalhados
    centros = np.stack([np.quantile(P, q, axis=0)
                        for q in np.linspace(0.05, 0.95, N_FILTROS)])
    for _ in range(12):
        d = ((P[:, None, :] - centros[None, :, :]) ** 2).sum(-1)
        grupo = d.argmin(1)
        for k in range(N_FILTROS):
            if (grupo == k).any():
                centros[k] = P[grupo == k].mean(0)

    saida = []
    for a1, a2 in centros:
        c1 = int(np.clip(round(a1 * 2048), -32768, 32767))
        c2 = int(np.clip(round(a2 * 2048), -32768, 32767))
        saida.append((c1, c2))
    return saida


def _decodificar_nibble(n, escala, c1, c2, ant1, ant2):
    v = ((n << escala) * 2048 + 1024 + c1 * ant1 + c2 * ant2) >> 11
    return max(-32768, min(32767, v))


def codificar(amostras, filtros):
    """ PCM16 -> bytes DSP-ADPCM. Devolve (dados, ps_inicial). """
    dados = bytearray()
    ant1 = ant2 = 0
    ps_inicial = None

    for i in range(0, len(amostras), QUADRO):
        # int do Python, não int16: `alvo << 11` estoura em 16 bits
        bloco = [int(v) for v in amostras[i:i + QUADRO]]
        melhor = None

        for p, (c1, c2) in enumerate(filtros):
            # Estima a escala pelo resíduo em malha aberta e só tenta a
            # vizinhança dela. Varrer as 16 daria o mesmo resultado gastando
            # cinco vezes mais tempo.
            a1, a2 = ant1, ant2
            pior = 0
            for alvo in bloco:
                r = abs(alvo - ((c1 * a1 + c2 * a2) >> 11))
                pior = max(pior, r)
                a2, a1 = a1, alvo
            e0 = 0
            while e0 < 15 and (7 << e0) < pior:
                e0 += 1
            for escala in range(max(0, e0 - 1), min(16, e0 + 3)):
                a1, a2 = ant1, ant2
                erro = 0
                nibbles = []
                for alvo in bloco:
                    pred = (c1 * a1 + c2 * a2)
                    # o nibble que mais aproxima, dentro de -8..7
                    ideal = ((alvo << 11) - pred - 1024) / (2048 << escala)
                    n = int(max(-8, min(7, round(ideal))))
                    v = _decodificar_nibble(n, escala, c1, c2, a1, a2)
                    erro += (v - alvo) ** 2
                    nibbles.append(n & 0xF)
                    a2, a1 = a1, v
                if melhor is None or erro < melhor[0]:
                    melhor = (erro, p, escala, nibbles, a1, a2)
                if erro == 0:
                    break
            if melhor and melhor[0] == 0:
                break

        _, p, escala, nibbles, a1, a2 = melhor
        cab = (p << 4) | escala
        if ps_inicial is None:
            ps_inicial = cab
        dados.append(cab)
        while len(nibbles) < QUADRO:
            nibbles.append(0)
        for k in range(0, QUADRO, 2):
            dados.append((nibbles[k] << 4) | nibbles[k + 1])
        ant1, ant2 = a1, a2

    return bytes(dados), (ps_inicial or 0)


def decodificar(dados, filtros, quantas):
    """ Só para conferir o que a gente escreveu. """
    saida = []
    ant1 = ant2 = 0
    for i in range(0, len(dados), 8):
        cab = dados[i]
        p, escala = cab >> 4, cab & 0xF
        c1, c2 = filtros[p % len(filtros)]
        for k in range(QUADRO):
            b = dados[i + 1 + k // 2]
            n = (b >> 4) if k % 2 == 0 else (b & 0xF)
            if n > 7:
                n -= 16
            v = _decodificar_nibble(n, escala, c1, c2, ant1, ant2)
            saida.append(v)
            ant2, ant1 = ant1, v
            if len(saida) >= quantas:
                return np.array(saida, dtype=np.int16)
    return np.array(saida, dtype=np.int16)


# ------------------------------------------------------------- o arquivo ---

def escrever_cwav(caminho, amostras, taxa, filtros, dados, ps):
    """ Copia fiel da planta do banner de varejo.

    Três detalhes vieram de comparar com o banner do MHXX, e nenhum deles é
    enfeite -- eram as diferenças que sobravam depois de acertar a codificação:

    1. As amostras começam 0x18 depois do payload do DATA, ou seja alinhadas
       em DATA+0x20, com 24 bytes de zeros antes. As nossas começavam coladas
       no cabeçalho, desalinhadas.
    2. Dois canais. O de varejo é estéreo; escrever mono era apostar que o
       tocador do menu aceita, e não há como testar essa aposta aqui.
    3. 32728 Hz, que é a taxa do 3DS -- não 32000.
    """
    n = len(amostras)

    # --- cabeçalho de cada canal (ambos com os mesmos dados) ---
    def canal(off_amostras):
        c = bytearray()
        c += struct.pack('<HHI', 0x1F00, 0, off_amostras)   # onde no DATA
        c += struct.pack('<HHI', 0x0300, 0, 0x28)           # info ADPCM
        c += struct.pack('<HHI', 0, 0, 0)                   # reservado
        while len(c) < 0x28:
            c += b'\x00'
        for c1, c2 in filtros:
            c += struct.pack('<hh', c1, c2)
        c += struct.pack('<Hhh', ps, 0, 0)                  # início
        c += struct.pack('<Hhh', ps, 0, 0)                  # contexto de laço
        c += struct.pack('<H', 0)
        while len(c) % 4:
            c += b'\x00'
        return bytes(c)

    ENCHE = 0x18                       # zeros antes das amostras, como no varejo
    corpo_canal = bytearray(dados)
    while len(corpo_canal) % 0x20:
        corpo_canal += b'\x00'
    off0 = ENCHE
    off1 = ENCHE + len(corpo_canal)

    tam_canal = len(canal(0))
    base_refs = 0x20                   # onde começa a tabela de referências
    off_ci0 = (base_refs - 0x1C) + 2 * 8
    off_ci1 = off_ci0 + tam_canal

    info = bytearray()
    info += b'INFO'
    info += struct.pack('<I', 0)
    info += struct.pack('<BBH', 2, 0, 0)          # DSP-ADPCM, sem laço
    info += struct.pack('<III', taxa, 0, n)
    info += struct.pack('<I', 0)
    info += struct.pack('<I', 2)                  # dois canais
    info += struct.pack('<HHI', 0x7100, 0, off_ci0)
    info += struct.pack('<HHI', 0x7100, 0, off_ci1)
    info += canal(off0)
    info += canal(off1)
    while len(info) % 0x20:
        info += b'\x00'
    struct.pack_into('<I', info, 4, len(info))

    corpo = bytearray(b'\x00' * ENCHE) + corpo_canal + corpo_canal
    while len(corpo) % 0x20:
        corpo += b'\x00'
    bloco_dados = b'DATA' + struct.pack('<I', len(corpo) + 8) + bytes(corpo)

    cab = bytearray()
    cab += b'CWAV'
    cab += struct.pack('<HH', 0xFEFF, 0x40)
    cab += struct.pack('<I', 0x02010000)
    cab += struct.pack('<I', 0)
    cab += struct.pack('<I', 2)
    off_info = 0x40
    off_data = off_info + len(info)
    cab += struct.pack('<HHII', 0x7000, 0, off_info, len(info))
    cab += struct.pack('<HHII', 0x7001, 0, off_data, len(bloco_dados))
    while len(cab) < 0x40:
        cab += b'\x00'

    total = len(cab) + len(info) + len(bloco_dados)
    struct.pack_into('<I', cab, 0x0C, total)

    with open(caminho, 'wb') as f:
        f.write(bytes(cab) + bytes(info) + bloco_dados)
    return total


def reamostrar(a, de, para):
    """ Interpolação linear. 32000 -> 32728 é 2% -- inaudível, mas é a taxa
    que o 3DS usa e não custa nada acertar. """
    if de == para:
        return a
    n = int(len(a) * para / de)
    pos = np.arange(n) * (de / para)
    i = np.clip(pos.astype(int), 0, len(a) - 2)
    f = pos - i
    return np.clip((a[i] * (1 - f) + a[i + 1] * f), -32768, 32767).astype(np.int16)


# ------------------------------------------------- streaming da música ----

GHA_MAGICA = b'GHA1'
GHA_CAB = 52          # tamanho do cabeçalho fixo

def gerar_gha(caminho, amostras, taxa, filtros, quadros_por_bloco=1024):
    """ Escreve a música em ADPCM para o app tocar por streaming.

    Por que um formato próprio e não o .wav
    ---------------------------------------
    PCM16 pesa 4x mais: a trilha sozinha eram 10,3 MB dos 10,7 do .cia. E o
    peso não é só espaço -- é leitura de cartão. Cada bloco de áudio custava
    16 KB de leitura no meio do laço principal, e é isso que engasga.

    O DSP do 3DS toca ADPCM direto, sem a CPU decodificar. Então o arquivo
    encolhe 4x E a leitura encolhe 4x, pelo mesmo motivo.

    Por que uma tabela de contexto
    ------------------------------
    Em ADPCM cada amostra depende das duas anteriores. Quem decodifica é o
    DSP, então o app NÃO sabe quais foram as duas últimas amostras de um bloco
    -- e sem isso não consegue dizer ao DSP como começar o bloco seguinte.

    A saída é calcular isso aqui, uma vez: decodificamos o que acabamos de
    codificar e anotamos o contexto no início de cada bloco. O app só lê a
    tabela. Custa 6 bytes por bloco.
    """
    amostras_por_bloco = quadros_por_bloco * QUADRO
    dados, ps0 = codificar(amostras, filtros)

    # contexto no começo de cada bloco: (ps, hist1, hist2)
    contextos = []
    ant1 = ant2 = 0
    bytes_por_bloco = quadros_por_bloco * 8
    for o in range(0, len(dados), bytes_por_bloco):
        pedaco = dados[o:o + bytes_por_bloco]
        contextos.append((pedaco[0] if pedaco else 0, ant1, ant2))
        # roda o decodificador para saber onde este bloco termina
        for i in range(0, len(pedaco), 8):
            cab = pedaco[i]
            p, escala = cab >> 4, cab & 0xF
            c1, c2 = filtros[p % len(filtros)]
            for k in range(QUADRO):
                b = pedaco[i + 1 + k // 2]
                n = (b >> 4) if k % 2 == 0 else (b & 0xF)
                if n > 7:
                    n -= 16
                v = _decodificar_nibble(n, escala, c1, c2, ant1, ant2)
                ant2, ant1 = ant1, v

    with open(caminho, 'wb') as f:
        f.write(GHA_MAGICA)
        f.write(struct.pack('<III', taxa, len(amostras), amostras_por_bloco))
        f.write(struct.pack('<I', len(contextos)))
        for c1, c2 in filtros:
            f.write(struct.pack('<hh', c1, c2))
        for ps, h1, h2 in contextos:
            f.write(struct.pack('<Hhh', ps, h1, h2))
        f.write(dados)

    total = GHA_CAB + len(contextos) * 6 + len(dados)
    return total, len(contextos)


def main():
    p = argparse.ArgumentParser(description='WAV -> CWAV DSP-ADPCM para o banner')
    p.add_argument('entrada')
    p.add_argument('-o', '--saida', default='banner.bcwav')
    p.add_argument('--gha', default='',
                   help='também escreve a música em ADPCM para streaming')
    p.add_argument('--segundos', type=float, default=0,
                   help='corta em N segundos (0 = inteiro)')
    p.add_argument('--taxa', type=int, default=32728,
                   help='taxa de saída (padrão %(default)s, a do 3DS)')
    args = p.parse_args()

    with wave.open(args.entrada, 'rb') as w:
        if w.getsampwidth() != 2:
            print('precisa ser PCM 16 bits', file=sys.stderr)
            return 1
        taxa = w.getframerate()
        a = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2')
        if w.getnchannels() > 1:
            a = a.reshape(-1, w.getnchannels()).mean(1).astype(np.int16)

    if args.segundos > 0:
        a = a[:int(args.segundos * taxa)]

    if taxa != args.taxa:
        a = reamostrar(a.astype(np.float64), taxa, args.taxa)
        taxa = args.taxa

    filtros = gerar_filtros(a)
    dados, ps = codificar(a, filtros)
    total = escrever_cwav(args.saida, a, taxa, filtros, dados, ps)

    # Conferir é obrigatório aqui: um codec escrito errado não dá erro, ele
    # dá barulho -- e quem descobre é quem liga o console.
    if args.gha:
        tam, nb = gerar_gha(args.gha, a, taxa, filtros)
        print(f'  {args.gha}  {tam} bytes em {nb} blocos  '
              f'({tam * 100 // (len(a) * 2)}% do PCM)')

    volta = decodificar(dados, filtros, len(a))
    m = min(len(volta), len(a))
    ruido = (a[:m].astype(float) - volta[:m].astype(float))
    sinal = a[:m].astype(float)
    snr = 10 * np.log10((sinal ** 2).sum() / max((ruido ** 2).sum(), 1e-9))
    print(f'  {len(a)} amostras a {taxa} Hz -> {total} bytes  '
          f'({total * 100 // (len(a) * 2)}% do PCM)')
    print(f'  qualidade: {snr:.1f} dB de relação sinal/ruído')
    if snr < 12:
        print('  AVISO: baixa demais -- o som vai sair sujo', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
