#!/usr/bin/env python3
# tools/gen_compress_vectors.py - genere les vecteurs de gw_compress_tests.
#
# Regle qui justifie ce fichier : les vecteurs ne doivent PAS sortir de notre
# propre code. Tout ce qui est ecrit ici s appuie sur le module `zlib` de la
# bibliotheque standard Python (implementation de reference independante) et,
# pour les references PNG, sur un decodeur PNG ecrit ici meme au-dessus de
# `zlib` - donc sans une ligne commune avec src/image/png.cpp.
#
# Usage : python tools/gen_compress_vectors.py <repertoire_de_sortie>
#
# Les tailles produites sont verifiees contre les valeurs documentees dans le
# README (tableau « Preuves »). Une derive de version de zlib fait echouer le
# script bruyamment plutot que d affaiblir silencieusement le banc de test :
# le volume de robustesse (102 troncatures, 306 mutations) depend directement
# de la taille de `dynamic.deflate`.

import os
import random
import struct
import sys
import zlib

# --- DEFLATE brut (RFC 1951, sans enveloppe) ---------------------------------


def deflate_raw(data, level=9):
    c = zlib.compressobj(level, zlib.DEFLATED, -15)
    return c.compress(data) + c.flush()


def first_block_type(stream):
    # BFINAL = bit 0, BTYPE = bits 1-2. 0 = stored, 1 = Huffman fixe,
    # 2 = Huffman dynamique.
    return (stream[0] >> 1) & 3


def first_block_final(stream):
    return bool(stream[0] & 1)


# Textes sources. La longueur de prefixe retenue pour `dynamic` n est pas
# esthetique : elle est choisie pour reproduire a l octet pres la taille
# compressee documentee a l origine (102 octets), et donc les memes volumes de
# robustesse (102 troncatures, 306 mutations).
PARA = (b"Un editeur de texte qui tient dans sa tete, et dont chaque octet ecrit "
        b"sur le disque a ete decide quelque part dans le code.\n")
PHRASE = b"Un editeur de texte qui tient dans sa tete.\n"


def repeat_to(base, size):
    return (base * (size // len(base) + 1))[:size]


def build_deflate_vectors():
    vectors = {}

    # stored : au niveau 0, zlib n emet que des blocs `stored` - 5 octets
    # d en-tete par bloc.
    stored_clear = repeat_to(b"stored ", 740)
    vectors["stored"] = (stored_clear, deflate_raw(stored_clear, 0), 0, 745)

    # fixed : trois octets, trop courts pour justifier un arbre dynamique.
    fixed_clear = b"abc"
    vectors["fixed"] = (fixed_clear, deflate_raw(fixed_clear), 1, 5)

    # dynamic : texte tres repetitif, arbre de Huffman dynamique.
    dynamic_clear = repeat_to(PARA[:55], 13200)
    vectors["dynamic"] = (dynamic_clear, deflate_raw(dynamic_clear), 2, 102)

    # mixed : du bruit (que zlib ne peut qu emettre en blocs `stored`, non
    # finaux) suivi de texte compressible - donc plusieurs blocs, de types
    # differents, dans un seul flux.
    rnd = random.Random(20240501)
    noise = bytes(rnd.randrange(256) for _ in range(69940))
    mixed_clear = noise + repeat_to(PHRASE, 97000 - len(noise))
    vectors["mixed"] = (mixed_clear, deflate_raw(mixed_clear), 0, 70212)

    return vectors


# --- Decodeur PNG de reference, ecrit au-dessus de zlib ----------------------
#
# Sortie : largeur et hauteur en little-endian sur 4 octets chacune, puis les
# pixels au format du Framebuffer maison - 0xAARRGGBB par pixel, ecrit en
# little-endian, origine en haut a gauche, alpha non premultiplie.

CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def decode_png(path):
    raw = open(path, "rb").read()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(path + " : signature PNG absente")

    pos = 8
    idat = bytearray()
    palette = b""
    trns = b""
    width = height = depth = color = 0
    interlace = 0
    while pos < len(raw):
        (length,) = struct.unpack(">I", raw[pos:pos + 4])
        kind = raw[pos + 4:pos + 8]
        payload = raw[pos + 8:pos + 8 + length]
        (crc,) = struct.unpack(">I", raw[pos + 8 + length:pos + 12 + length])
        if zlib.crc32(kind + payload) & 0xFFFFFFFF != crc:
            raise SystemExit(path + " : CRC invalide")
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", payload)
        elif kind == b"PLTE":
            palette = payload
        elif kind == b"tRNS":
            trns = payload
        elif kind == b"IDAT":
            idat += payload
        elif kind == b"IEND":
            break
        pos += 12 + length

    if depth != 8 or interlace != 0 or color not in CHANNELS:
        raise SystemExit(path + " : hors du sous-ensemble supporte")

    n = CHANNELS[color]
    stride = width * n
    data = zlib.decompress(bytes(idat))
    if len(data) != (stride + 1) * height:
        raise SystemExit(path + " : taille decompressee inattendue")

    lines = []
    prev = bytearray(stride)
    off = 0
    for _ in range(height):
        ftype = data[off]
        line = bytearray(data[off + 1:off + 1 + stride])
        off += 1 + stride
        for i in range(stride):
            a = line[i - n] if i >= n else 0
            b = prev[i]
            c = prev[i - n] if i >= n else 0
            if ftype == 1:
                line[i] = (line[i] + a) & 0xFF
            elif ftype == 2:
                line[i] = (line[i] + b) & 0xFF
            elif ftype == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif ftype == 4:
                line[i] = (line[i] + paeth(a, b, c)) & 0xFF
            elif ftype != 0:
                raise SystemExit(path + " : filtre inconnu")
        lines.append(line)
        prev = line

    out = bytearray(struct.pack("<II", width, height))
    for line in lines:
        for x in range(width):
            px = line[x * n:(x + 1) * n]
            alpha = 255
            if color == 0:
                r = g = b = px[0]
            elif color == 4:
                r = g = b = px[0]
                alpha = px[1]
            elif color == 2:
                r, g, b = px
            elif color == 6:
                r, g, b, alpha = px
            else:  # color == 3, index de palette
                i = px[0]
                r, g, b = palette[i * 3:i * 3 + 3]
                if i < len(trns):
                    alpha = trns[i]
            out += struct.pack("<I", (alpha << 24) | (r << 16) | (g << 8) | b)
    return bytes(out)


# --- Ecriture ----------------------------------------------------------------


def main():
    if len(sys.argv) < 2:
        print("usage : " + sys.argv[0] + " <repertoire_de_sortie>")
        return 2
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    def write(name, payload):
        with open(os.path.join(out_dir, name), "wb") as f:
            f.write(payload)
        print("  %-24s %8d octets" % (name, len(payload)))

    print("Blocs DEFLATE :")
    for name, (clear, compressed, want_type, want_size) in build_deflate_vectors().items():
        got_type = first_block_type(compressed)
        if got_type != want_type:
            raise SystemExit(name + " : type de premier bloc inattendu")
        if len(compressed) != want_size:
            raise SystemExit(
                "%s : %d octets compresses, %d attendus. La version de zlib a "
                "change : ajuster le script ET le README, sinon les volumes de "
                "robustesse documentes ne tiennent plus."
                % (name, len(compressed), want_size))
        if name == "mixed" and first_block_final(compressed):
            raise SystemExit("mixed : le premier bloc devrait etre non final")
        write(name + ".deflate", compressed)
        write(name + ".expected", clear)

    print("Enveloppe zlib :")
    wrapped_clear = repeat_to(PARA, 8192)
    write("wrapped.zlib", zlib.compress(wrapped_clear, 9))
    write("wrapped.expected", wrapped_clear)
    write("wrapped.adler", str(zlib.adler32(wrapped_clear) & 0xFFFFFFFF).encode())

    print("References PNG :")
    for logo in ("icone_write_w", "icone_write_b"):
        png = os.path.join(root, "assets", "branding", logo + ".png")
        write(logo + ".rgba", decode_png(png))

    print("")
    print("Vecteurs ecrits dans " + out_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
