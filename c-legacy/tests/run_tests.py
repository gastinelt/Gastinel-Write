"""Tests des modules ODT/zip/inflate de la version C, compiles nativement avec ASan.
Les .odt de samples/ viennent de la version C++ (copies depuis la branche main)."""
import io, os, random, re, subprocess, sys, tempfile, zipfile, html

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(tempfile.gettempdir(), "gw_odt_tool")
SAMPLES = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "samples")
failures = 0

def check(name, cond, detail=""):
    global failures
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  " + detail))
    failures += not cond

def run(*args):
    return subprocess.run([TOOL, *args], capture_output=True)

def expected_text(xml):
    paras = re.findall(r'<text:p(?:\s[^>]*?)?(?:/>|>(.*?)</text:p>)', xml, re.S)
    return "\n".join(html.unescape(re.sub(r"<[^>]+>", "", p)) for p in paras)

subprocess.check_call(["gcc", "-std=c89", "-Wall", "-Wextra", "-Werror", "-g",
    "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-o", TOOL,
    os.path.join(HERE, "odt_tool.c"), *[os.path.join(HERE, "../src", f) for f in
    ("core.c", "inflate.c", "zip.c", "odt.c")]])

tmp = tempfile.mkdtemp()
for name in ("plain", "legacy", "roundtrip", "roundtrip_format"):
    path = os.path.join(SAMPLES, name + ".odt")
    xml = zipfile.ZipFile(path).read("content.xml").decode()
    want = expected_text(xml)
    r = run("cat", path)
    check(f"lecture {name}.odt (C++)", r.returncode == 0 and r.stdout.decode() == want,
          repr(r.stdout[:200]) + r.stderr.decode())
    check(f"detection mise en forme {name}.odt",
          (b"FORMATTED" in r.stderr) == (name == "roundtrip_format"))

    # Meme contenu recompresse en DEFLATE (niveaux 0..9), comme LibreOffice.
    for level in (1, 6, 9):
        p = os.path.join(tmp, f"{name}-{level}.odt")
        with zipfile.ZipFile(p, "w") as z:
            z.writestr(zipfile.ZipInfo("mimetype"), "application/vnd.oasis.opendocument.text")
            z.writestr("content.xml", xml, compress_type=zipfile.ZIP_DEFLATED, compresslevel=level)
        r = run("cat", p)
        check(f"lecture {name}.odt deflate niveau {level}",
              r.returncode == 0 and r.stdout.decode() == want, r.stderr.decode())

# Gros document compresse : blocs dynamiques, longues references arriere.
rng = random.Random(1)
words = ["écriture", "sans", "fard", "Gastinel", "<&>", "\"q\"", "texte", "ligne"]
big = "\n".join(" ".join(rng.choice(words) for _ in range(rng.randint(0, 30))) for _ in range(5000))
xml = ('<?xml version="1.0"?><office:document-content><office:body><office:text>' +
       "".join(f"<text:p>{html.escape(l, quote=True)}</text:p>" if l else "<text:p/>" for l in big.split("\n")) +
       "</office:text></office:body></office:document-content>")
p = os.path.join(tmp, "big.odt")
with zipfile.ZipFile(p, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("content.xml", xml)
r = run("cat", p)
check(f"gros document deflate ({len(xml)//1024} Ko)", r.returncode == 0 and r.stdout.decode() == big, r.stderr.decode())

# Ecriture : doit produire le meme content.xml que la version C++.
src = os.path.join(SAMPLES, "roundtrip.txt")
out = os.path.join(tmp, "rt.odt")
check("ecriture odt", run("make", src, out).returncode == 0)
ours, cpp = zipfile.ZipFile(out), zipfile.ZipFile(os.path.join(SAMPLES, "roundtrip.odt"))
check("archive valide (testzip)", ours.testzip() is None)
check("mimetype en premier, non compresse",
      ours.infolist()[0].filename == "mimetype" and ours.infolist()[0].compress_type == 0)
for entry in ("mimetype", "META-INF/manifest.xml", "styles.xml", "content.xml"):
    check(f"{entry} identique au C++", ours.read(entry) == cpp.read(entry))
r = run("cat", out)
check("aller-retour txt -> odt -> txt",
      r.stdout.decode() == open(src, encoding="utf-8").read().replace("\r\n", "\n").rstrip("\n"))

# Fichiers LibreOffice : titres, tabulations, espaces, sauts de ligne.
lo = ('<office:document-content><office:body><office:text>'
      '<text:h text:outline-level="1">Titre</text:h>'
      '<text:p text:style-name="P1">a<text:tab/>b<text:s text:c="3"/>c<text:line-break/>d<text:s/>e</text:p>'
      '<text:p><text:span text:style-name="T1">&#233;t&#xE9;</text:span></text:p>'
      '<text:page-number>9</text:page-number></office:text></office:body></office:document-content>')
p = os.path.join(tmp, "lo.odt")
with zipfile.ZipFile(p, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("content.xml", lo)
r = run("cat", p)
check("fichier style LibreOffice", r.stdout.decode() == "Titre\na\tb   c\nd e\nété", repr(r.stdout))

# Robustesse : fichiers tronques et corrompus -> erreur propre, jamais de plantage.
blob = open(os.path.join(tmp, "big.odt"), "rb").read()
crashes = 0
for i in range(300):
    data = bytearray(blob)
    if i % 3 == 0:
        data = data[: rng.randint(0, len(data))]
    else:
        for _ in range(rng.randint(1, 20)):
            data[rng.randrange(len(data))] = rng.randrange(256)
    p = os.path.join(tmp, "fuzz.odt")
    open(p, "wb").write(data)
    r = run("cat", p)
    if r.returncode not in (0, 1):
        crashes += 1
        if crashes == 1: print(r.stderr.decode()[-1500:])
check("300 fichiers corrompus sans plantage (ASan)", crashes == 0, f"{crashes} plantages")

print("\nECHECS :", failures)
sys.exit(1 if failures else 0)
