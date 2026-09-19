#!/usr/bin/env python3
""" Desenha um modelo COLLADA (.dae) na imagem do banner do .cia.

Por que isto existe
-------------------
O banner do menu HOME é uma cena CGFX. O `bannertool` monta essa cena sozinho
quando recebe um PNG -- uma placa plana com a imagem -- e aceita um .cgfx
pronto para ter geometria e animação de verdade.

O problema é que CGFX é formato da Nintendo, do CTR SDK, e não existe
ferramenta aberta que ESCREVA um. As da comunidade leem. Então converter um
.dae em .cgfx não é caminho.

O que dá para fazer, e é o que este programa faz: renderizar o modelo aqui,
uma vez, e usar o resultado como a imagem do banner. O banner continua sendo
uma placa plana -- mas com o boneco desenhado nela, iluminado e no ângulo que
a gente escolher, em vez de um desenho chapado.

    python3 renderizar_banner.py "<modelo.dae>" -o banner-proprio.png

O modelo e a textura são de quem monta o build. A saída padrão
(`banner-proprio.png`) é ignorada pelo git de propósito.
"""

import argparse
import os
import re
import sys

import numpy as np
from PIL import Image

CARMIM = (0x9B, 0x2E, 0x2E)
OURO = (0xC9, 0x9B, 0x38)
CLARA = (0xF2, 0xE9, 0xD4)
NOITE = (0x1A, 0x16, 0x12)


# --------------------------------------------------------------- COLLADA ---

def _floats(texto):
    return np.array([float(v) for v in texto.split()], dtype=np.float64)


def ler_dae(caminho):
    """ Devolve (vertices, uvs, normais) já triangulados, um por índice.

    COLLADA guarda três listas separadas e um vetor de índices que aponta para
    todas de uma vez, com um passo por fonte. Ler isso errado dá um boneco
    embaralhado, então o passo (`offset`) é lido do arquivo, não suposto.
    """
    x = open(caminho, encoding='utf-8', errors='replace').read()

    fontes = {}
    for m in re.finditer(r'<source id="([^"]+)".*?<float_array[^>]*>(.*?)</float_array>',
                         x, re.S):
        fontes[m.group(1)] = _floats(m.group(2))

    # <vertices> é um apelido para a fonte de posições
    # A ordem dos atributos varia entre exportadores -- este arquivo põe
    # offset ANTES de semantic. Ler atributo por atributo evita depender disso.
    def atributos(tag):
        return dict(re.findall(r'(\w+)="([^"]*)"', tag))

    apelidos = {}
    for m in re.finditer(r'<vertices\b([^>]*)>(.*?)</vertices>', x, re.S):
        alvo = atributos(m.group(1)).get('id')
        for t in re.findall(r'<input\b[^>]*>', m.group(2)):
            a = atributos(t)
            if a.get('semantic') == 'POSITION':
                apelidos[alvo] = a['source'].lstrip('#')

    bloco = re.search(r'<(polylist|triangles)[^>]*count="(\d+)"[^>]*>(.*?)</\1>', x, re.S)
    if not bloco:
        raise ValueError('não achei <polylist> nem <triangles> no arquivo')
    corpo = bloco.group(3)

    entradas = {}
    for t in re.findall(r'<input\b[^>]*>', corpo):
        a = atributos(t)
        if 'semantic' not in a or 'source' not in a:
            continue
        fonte = a['source'].lstrip('#')
        entradas[a['semantic']] = (apelidos.get(fonte, fonte), int(a.get('offset', 0)))
    if not entradas:
        raise ValueError('não achei os <input> da malha')

    passo = max(off for _, off in entradas.values()) + 1
    idx = np.array([int(v) for v in re.search(r'<p>(.*?)</p>', corpo, re.S).group(1).split()])
    idx = idx.reshape(-1, passo)

    vc = re.search(r'<vcount>(.*?)</vcount>', corpo, re.S)
    vcount = [int(v) for v in vc.group(1).split()] if vc else [3] * (len(idx) // 3)

    # Triangula em leque: um quadrilátero vira dois triângulos. Sem isto, um
    # modelo com quads perde metade das faces e fica cheio de buracos.
    tris, o = [], 0
    for n in vcount:
        for k in range(1, n - 1):
            tris.append([o, o + k, o + k + 1])
        o += n
    tris = np.array(tris)

    def pega(sem, largura):
        if sem not in entradas:
            return None
        nome, off = entradas[sem]
        dados = fontes[nome].reshape(-1, largura)
        return dados[idx[:, off]]

    pos = pega('VERTEX', 3)
    uv = pega('TEXCOORD', 2)
    nor = pega('NORMAL', 3)
    if pos is None:
        raise ValueError('o modelo não tem posições')
    if uv is None:
        uv = np.zeros((len(pos), 2))
    if nor is None:
        nor = np.zeros((len(pos), 3))
    return pos[tris], uv[tris], nor[tris]


# -------------------------------------------------------------- desenho ----

def girar(p, giro, inclina):
    cy, sy = np.cos(giro), np.sin(giro)
    cx, sx = np.cos(inclina), np.sin(inclina)
    ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    return p @ ry.T @ rx.T


def renderizar(tri, uv, nor, textura, larg, alt, giro, inclina, escala, subamostra=3):
    """ Rasteriza com z-buffer, textura e uma luz. Desenha grande e reduz --
    é o jeito barato de ter borda macia sem antialias de verdade. """
    L, A = larg * subamostra, alt * subamostra

    v = girar(tri.reshape(-1, 3), giro, inclina).reshape(tri.shape)
    n = girar(nor.reshape(-1, 3), giro, inclina).reshape(nor.shape)

    centro = (v.reshape(-1, 3).min(0) + v.reshape(-1, 3).max(0)) / 2
    v = v - centro
    tamanho = np.abs(v.reshape(-1, 3)).max()
    v = v / tamanho * (min(L, A) * 0.5 * escala)

    sx = v[:, :, 0] + L / 2
    sy = -v[:, :, 1] + A / 2
    sz = v[:, :, 2]

    cor = np.zeros((A, L, 3), dtype=np.float64)
    alfa = np.zeros((A, L), dtype=np.float64)
    prof = np.full((A, L), 1e9)

    tw, th = textura.size
    tex = np.asarray(textura.convert('RGB'), dtype=np.float64) / 255.0

    luz = np.array([0.4, 0.7, 0.6])
    luz = luz / np.linalg.norm(luz)

    for i in range(len(v)):
        x0, x1, x2 = sx[i]
        y0, y1, y2 = sy[i]
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(area) < 1e-9:
            continue

        minx = max(int(np.floor(min(x0, x1, x2))), 0)
        maxx = min(int(np.ceil(max(x0, x1, x2))), L - 1)
        miny = max(int(np.floor(min(y0, y1, y2))), 0)
        maxy = min(int(np.ceil(max(y0, y1, y2))), A - 1)
        if minx > maxx or miny > maxy:
            continue

        yy, xx = np.mgrid[miny:maxy + 1, minx:maxx + 1]
        px, py = xx + 0.5, yy + 0.5
        w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area
        w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area
        w2 = 1.0 - w0 - w1
        dentro = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not dentro.any():
            continue

        z = w0 * sz[i, 0] + w1 * sz[i, 1] + w2 * sz[i, 2]
        melhor = dentro & (z < prof[miny:maxy + 1, minx:maxx + 1])
        if not melhor.any():
            continue

        u = w0 * uv[i, 0, 0] + w1 * uv[i, 1, 0] + w2 * uv[i, 2, 0]
        vv = w0 * uv[i, 0, 1] + w1 * uv[i, 1, 1] + w2 * uv[i, 2, 1]
        tx = np.clip((u % 1.0) * (tw - 1), 0, tw - 1).astype(int)
        ty = np.clip((1.0 - (vv % 1.0)) * (th - 1), 0, th - 1).astype(int)
        base = tex[ty, tx]

        nn = (w0[..., None] * n[i, 0] + w1[..., None] * n[i, 1] + w2[..., None] * n[i, 2])
        comp = np.linalg.norm(nn, axis=-1, keepdims=True)
        nn = np.divide(nn, comp, out=np.zeros_like(nn), where=comp > 1e-9)
        difusa = np.clip((nn * luz).sum(-1), 0, 1)
        sombra = (0.45 + 0.55 * difusa)[..., None]

        alvo_cor = cor[miny:maxy + 1, minx:maxx + 1]
        alvo_prof = prof[miny:maxy + 1, minx:maxx + 1]
        alvo_alfa = alfa[miny:maxy + 1, minx:maxx + 1]
        alvo_cor[melhor] = (base * sombra)[melhor]
        alvo_prof[melhor] = z[melhor]
        alvo_alfa[melhor] = 1.0

    img = Image.fromarray((np.clip(cor, 0, 1) * 255).astype(np.uint8), 'RGB')
    msk = Image.fromarray((alfa * 255).astype(np.uint8), 'L')
    return (img.resize((larg, alt), Image.LANCZOS),
            msk.resize((larg, alt), Image.LANCZOS))


def montar_banner(boneco, mascara, larg, alt):
    """ Põe o boneco na moldura da guilda, com o letreiro à esquerda. """
    from PIL import ImageDraw, ImageFont
    import glob

    fundo = Image.new('RGB', (larg, alt), NOITE)
    d = ImageDraw.Draw(fundo)
    d.rectangle([0, alt - 5, larg, alt], fill=CARMIM)

    fundo.paste(boneco, (larg - boneco.width - 4, 0), mascara)

    fontes = [f for f in glob.glob('/usr/share/fonts/**/*.ttf', recursive=True)
              if 'DejaVuSans-Bold' in f or 'LiberationSans-Bold' in f]
    if fontes:
        g = ImageFont.truetype(fontes[0], 26)
        p = ImageFont.truetype(fontes[0], 9)
        d.text((12, 30), "GUILD", font=g, fill=CLARA)
        d.text((12, 58), "HUNTER", font=g, fill=OURO)
        d.text((12, 92), "cadastro nas guildas locais", font=p, fill=CLARA)
    return fundo


def main():
    p = argparse.ArgumentParser(description='Renderiza um .dae na imagem do banner')
    p.add_argument('modelo')
    p.add_argument('-o', '--saida', default='banner-proprio.png')
    p.add_argument('-t', '--textura', default='')
    p.add_argument('--giro', type=float, default=145.0, help='graus')
    p.add_argument('--inclina', type=float, default=12.0, help='graus')
    p.add_argument('--escala', type=float, default=0.95)
    p.add_argument('--largura', type=int, default=256)
    p.add_argument('--altura', type=int, default=128)
    p.add_argument('--so-o-boneco', action='store_true',
                   help='sem moldura nem letreiro')
    args = p.parse_args()

    tri, uv, nor = ler_dae(args.modelo)
    print(f'  {len(tri)} triângulos')

    textura = args.textura
    if not textura:
        pasta = os.path.dirname(os.path.abspath(args.modelo))
        achados = [f for f in os.listdir(pasta) if f.lower().endswith('.png')]
        if not achados:
            print('  sem textura ao lado do modelo', file=sys.stderr)
            return 1
        textura = os.path.join(pasta, achados[0])
    print(f'  textura {os.path.basename(textura)}')

    lado = args.altura
    boneco, mascara = renderizar(tri, uv, nor, Image.open(textura),
                                 lado, lado, np.radians(args.giro),
                                 np.radians(args.inclina), args.escala)
    saida = (boneco if args.so_o_boneco
             else montar_banner(boneco, mascara, args.largura, args.altura))
    saida.save(args.saida)
    print(f'  {args.saida}  {saida.size[0]}x{saida.size[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
