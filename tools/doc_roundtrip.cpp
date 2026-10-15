// tools/doc_roundtrip.cpp — verification des E/S documents, hors interface.
//
// Ecrit un document, le relit, et compare paragraphe par paragraphe. Cest la
// seule preuve qui compte pour un format de fichier : quun fichier ecrit se
// relise a lidentique.
//
// Cible separee du binaire applicatif : `cmake --build build --target
// gw_doc_roundtrip`, puis executer en lui passant un repertoire de travail.
#include "doc/document.h"
#include "doc/odt_io.h"
#include "doc/style.h"
#include "doc/text_edit.h"
#include "doc/txt_io.h"
#include "doc/zip.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    std::printf("%s  %s\n", condition ? "[ OK ]" : "[FAIL]", what.c_str());
    if (!condition) ++failures;
}

gw::doc::Document make_document(const std::vector<std::string>& lines) {
    gw::doc::Document doc;
    for (const std::string& l : lines) doc.paragraphs().push_back(gw::doc::Paragraph{l});
    return doc;
}

// Egalite COMPLETE : texte, attributs de paragraphe et plages de style. Se
// contenter du texte ferait passer le round-trip du formatage pour reussi alors
// quil aurait tout perdu — cest precisement ce quon veut prouver ici.
bool same(const gw::doc::Document& a, const gw::doc::Document& b) {
    if (a.paragraphs().size() != b.paragraphs().size()) return false;
    for (std::size_t i = 0; i < a.paragraphs().size(); ++i) {
        const gw::doc::Paragraph& x = a.paragraphs()[i];
        const gw::doc::Paragraph& y = b.paragraphs()[i];
        if (x.text != y.text) return false;
        if (x.heading != y.heading || x.list != y.list || x.align != y.align) return false;
        if (x.table_id != y.table_id || x.table_row != y.table_row || x.table_col != y.table_col ||
            x.table_cols != y.table_cols) {
            return false;
        }
        if (x.spans.size() != y.spans.size()) return false;
        for (std::size_t k = 0; k < x.spans.size(); ++k) {
            if (!(x.spans[k] == y.spans[k])) return false;
        }
    }
    return true;
}

// Forme lisible dun paragraphe stylise, pour que lechec dise ou est la
// difference plutot que « ce nest pas pareil ».
std::string describe(const gw::doc::Paragraph& p) {
    std::string out;
    switch (p.heading) {
        case gw::doc::HeadingLevel::H1: out += "H1 "; break;
        case gw::doc::HeadingLevel::H2: out += "H2 "; break;
        case gw::doc::HeadingLevel::H3: out += "H3 "; break;
        case gw::doc::HeadingLevel::None: break;
    }
    switch (p.list) {
        case gw::doc::ListKind::Bullet: out += "puce "; break;
        case gw::doc::ListKind::Numbered: out += "num "; break;
        case gw::doc::ListKind::None: break;
    }
    switch (p.align) {
        case gw::doc::Alignment::Center: out += "centre "; break;
        case gw::doc::Alignment::Right: out += "droite "; break;
        case gw::doc::Alignment::Left: break;
    }
    if (p.spans.empty()) return out + "[ ]" + p.text;

    std::size_t offset = 0;
    for (const gw::doc::StyleSpan& span : p.spans) {
        out += "[";
        out += span.style.bold ? 'b' : ' ';
        out += span.style.italic ? 'i' : ' ';
        out += span.style.underline ? 'u' : ' ';
        if (!span.style.font.empty()) out += "=" + span.style.font;
        out += "]" + p.text.substr(offset, span.length);
        offset += span.length;
    }
    return out;
}

void dump(const char* label, const gw::doc::Document& d) {
    std::printf("       %s (%zu paragraphes)\n", label, d.paragraphs().size());
    for (std::size_t i = 0; i < d.paragraphs().size(); ++i) {
        std::printf("         [%zu] %s\n", i, describe(d.paragraphs()[i]).c_str());
    }
}

// Applique un style de caractere a une plage doctets du paragraphe.
gw::doc::Paragraph styled(const std::string& text, std::size_t begin, std::size_t end,
                          bool bold, bool italic, bool underline, const std::string& font) {
    gw::doc::Paragraph p{text};
    gw::doc::style_apply(p.spans, p.text.size(), begin, end, [&](gw::doc::CharStyle& s) {
        s.bold = bold;
        s.italic = italic;
        s.underline = underline;
        s.font = font;
    });
    return p;
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    std::string error;

    // Jeu de test : accents multi-octets, caracteres a echapper en XML,
    // paragraphe vide au milieu, espaces multiples, ligne vide finale.
    const gw::doc::Document original = make_document({
        "Gastinel Write — essai de round-trip",
        "Accents : eaeiou, ligature oe, guillemets francais",
        "XML a echapper : <balise> & \"guillemets\" 'apostrophe'",
        "",
        "Espaces    multiples\tet tabulation",
        "Derniere ligne",
    });

    // --- TXT ---
    {
        const std::string path = dir + "/roundtrip.txt";
        check(gw::doc::save_txt(path, original, error), "save_txt : ecriture " + error);

        gw::doc::Document reloaded;
        const bool loaded = gw::doc::load_txt(path, reloaded, error);
        check(loaded, "load_txt : lecture " + (loaded ? std::string() : error));

        const bool equal = same(original, reloaded);
        check(equal, "TXT : round-trip identique");
        if (!equal) {
            dump("attendu", original);
            dump("obtenu", reloaded);
        }
    }

    // --- ODT ---
    {
        const std::string path = dir + "/roundtrip.odt";
        check(gw::doc::save_odt(path, original, error), "save_odt : ecriture " + error);

        gw::doc::Document reloaded;
        const bool loaded = gw::doc::load_odt(path, reloaded, error);
        check(loaded, "load_odt : lecture " + (loaded ? std::string() : error));

        const bool equal = same(original, reloaded);
        check(equal, "ODT : round-trip identique");
        if (!equal) {
            dump("attendu", original);
            dump("obtenu", reloaded);
        }
    }

    // --- Tableau : grille, cellules vides, mise en forme, texte avant et apres ---
    {
        gw::doc::Document with_table = make_document({"avant", "", "apres"});
        gw::doc::TextEdit edit;
        edit.reset(with_table);
        edit.set_caret(gw::doc::Position{1, 0}, false);
        edit.insert_table(2, 3);
        edit.insert_text("cellule");
        edit.move_cell(false);
        edit.toggle_bold();
        edit.insert_text("gras");
        const gw::doc::Document table_doc = edit.document();

        const std::string path = dir + "/roundtrip_table.odt";
        check(gw::doc::save_odt(path, table_doc, error), "save_odt (tableau) : ecriture " + error);

        gw::doc::Document reloaded;
        const bool loaded = gw::doc::load_odt(path, reloaded, error);
        check(loaded, "load_odt (tableau) : lecture " + (loaded ? std::string() : error));
        const bool equal = same(table_doc, reloaded);
        check(equal, "ODT : le tableau survit a l aller-retour a l identique");
        if (!equal) {
            dump("attendu", table_doc);
            dump("obtenu", reloaded);
        }
    }

    // --- Structure de larchive ODT ---
    {
        const std::string path = dir + "/roundtrip.odt";
        std::string bytes;
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (f != nullptr) {
            char buf[4096];
            std::size_t n = 0;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.append(buf, n);
            std::fclose(f);
        }
        check(bytes.size() > 4 && bytes.compare(0, 2, "PK") == 0, "ODT : signature zip presente");

        // Le nom `mimetype` doit apparaitre juste apres le premier en-tete
        // local, et son contenu doit etre lisible en clair (donc non compresse).
        check(bytes.find("mimetype") == 30, "ODT : mimetype est la premiere entree");
        check(bytes.find("application/vnd.oasis.opendocument.text") == 38,
              "ODT : mimetype stocke en clair, non compresse");

        gw::doc::ZipReader zip;
        if (zip.open(bytes, error)) {
            std::string names;
            for (const std::string& n : zip.entry_names()) names += n + " ";
            std::printf("       entrees : %s\n", names.c_str());
            check(zip.entry_names().size() == 4, "ODT : quatre entrees dans larchive");
        } else {
            check(false, "ODT : relecture du repertoire central " + error);
        }
    }

    // --- ODT : round-trip de la MISE EN FORME -------------------------------
    //
    // Le document ci-dessous couvre toutes les combinaisons que le schema sait
    // porter : les trois niveaux de titre, les deux types de liste, les trois
    // alignements, les trois attributs de caractere isoles puis combines, une
    // police par plage, et les cas limites (paragraphe vide stylise, plage en
    // tout debut et en toute fin, texte a echapper A LINTERIEUR dun span).
    {
        gw::doc::Document rich;
        auto& ps = rich.paragraphs();

        gw::doc::Paragraph h1{"Titre de niveau 1"};
        h1.heading = gw::doc::HeadingLevel::H1;
        ps.push_back(h1);

        gw::doc::Paragraph h2{"Titre de niveau 2, centre"};
        h2.heading = gw::doc::HeadingLevel::H2;
        h2.align = gw::doc::Alignment::Center;
        ps.push_back(h2);

        gw::doc::Paragraph h3{"Titre de niveau 3, a droite"};
        h3.heading = gw::doc::HeadingLevel::H3;
        h3.align = gw::doc::Alignment::Right;
        ps.push_back(h3);

        gw::doc::Paragraph bullet{"Un point de liste a puce"};
        bullet.list = gw::doc::ListKind::Bullet;
        ps.push_back(bullet);

        gw::doc::Paragraph numbered{"Un point de liste numerotee, centre"};
        numbered.list = gw::doc::ListKind::Numbered;
        numbered.align = gw::doc::Alignment::Center;
        ps.push_back(numbered);

        // Attributs isoles, chacun sur une plage interne.
        ps.push_back(styled("un mot gras ici", 3, 11, true, false, false, ""));
        ps.push_back(styled("un mot italique ici", 3, 15, false, true, false, ""));
        ps.push_back(styled("un mot souligne ici", 3, 15, false, false, true, ""));

        // Les trois a la fois, plus une police : le cas le plus charge.
        ps.push_back(styled("tout a la fois ici", 0, 14, true, true, true, "Fira Sans"));

        // Plage collee au debut, plage collee a la fin.
        ps.push_back(styled("debut en gras puis normal", 0, 13, true, false, false, ""));
        ps.push_back(styled("normal puis fin en gras", 12, 23, true, false, false, ""));

        // Caracteres a echapper A LINTERIEUR dune plage stylisee : cest le cas
        // qui casse une implementation qui echapperait avant de baliser.
        ps.push_back(styled("XML <balise> & \"guillemets\" en gras", 4, 34, true, false, false,
                            ""));

        // Accents multi-octets a cheval sur une frontiere de plage : verifie que
        // les longueurs sont bien comptees en OCTETS de bout en bout.
        ps.push_back(styled("ecrire un resume detaille", 10, 17, false, true, false, ""));

        // Paragraphe VIDE mais stylise : il sort en <text:p .../> auto-ferme, et
        // ses attributs doivent survivre a laller-retour.
        gw::doc::Paragraph empty_centered;
        empty_centered.align = gw::doc::Alignment::Center;
        empty_centered.list = gw::doc::ListKind::Bullet;
        ps.push_back(empty_centered);

        // Paragraphe sans aucune mise en forme, melange aux autres.
        ps.push_back(gw::doc::Paragraph{"Un paragraphe tout a fait ordinaire"});

        const std::string path = dir + "/roundtrip_format.odt";
        check(gw::doc::save_odt(path, rich, error), "save_odt (mise en forme) : ecriture " + error);

        gw::doc::Document reloaded;
        const bool loaded = gw::doc::load_odt(path, reloaded, error);
        check(loaded, "load_odt (mise en forme) : lecture " + (loaded ? std::string() : error));

        const bool equal = same(rich, reloaded);
        check(equal, "ODT : la mise en forme survit a laller-retour a lidentique");
        if (!equal) {
            dump("attendu", rich);
            dump("obtenu", reloaded);
        }
    }

    // --- ODT : compatibilite ascendante -------------------------------------
    //
    // Un content.xml tel quen produisaient les versions PRECEDENTES : aucun
    // espace de noms gw:, aucun attribut, aucun <text:span>. Il doit souvrir
    // sans erreur et sans le moindre style applique.
    //
    // Le fichier est fabrique ici plutot que garde en binaire dans le depot :
    // cest la forme exacte que produisait build_content_xml() avant ce
    // chantier, et lecrire noir sur blanc dit ce qui est teste.
    {
        gw::doc::ZipWriter zip;
        zip.add("mimetype", "application/vnd.oasis.opendocument.text");
        zip.add("META-INF/manifest.xml",
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<manifest:manifest"
                " xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\"/>\n");
        zip.add("content.xml",
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<office:document-content"
                " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
                " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
                " office:version=\"1.2\">\n <office:body>\n  <office:text>\n"
                "   <text:p>Ancien fichier</text:p>\n"
                "   <text:p/>\n"
                "   <text:p>Avec &amp; des entites</text:p>\n"
                "  </office:text>\n </office:body>\n</office:document-content>\n");

        const std::string path = dir + "/legacy.odt";
        const std::string bytes = zip.build();
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (f != nullptr) {
            std::fwrite(bytes.data(), 1, bytes.size(), f);
            std::fclose(f);
        }

        gw::doc::Document legacy;
        const bool ok = gw::doc::load_odt(path, legacy, error);
        check(ok, "ODT ancien format : lecture " + (ok ? std::string() : error));

        bool plain = ok && legacy.paragraphs().size() == 3;
        if (plain) {
            for (const gw::doc::Paragraph& p : legacy.paragraphs()) plain = plain && p.is_plain();
        }
        check(plain, "ODT ancien format : relu sans aucun style applique");
        check(ok && legacy.paragraphs().size() == 3 &&
                  legacy.paragraphs()[0].text == "Ancien fichier" &&
                  legacy.paragraphs()[1].text.empty() &&
                  legacy.paragraphs()[2].text == "Avec & des entites",
              "ODT ancien format : texte et entites intacts");
    }

    // --- ODT : un document sans style produit lANCIEN fichier ---------------
    //
    // Corollaire du point precedent, dans lautre sens : tant quaucune mise en
    // forme nest posee, le content.xml ne contient aucune trace du nouveau
    // schema. Cest ce qui garantit quon na pas alourdi les fichiers existants.
    {
        const gw::doc::Document plain = make_document({"Sans style", "", "Deux paragraphes"});
        const std::string path = dir + "/plain.odt";
        check(gw::doc::save_odt(path, plain, error), "save_odt (sans style) : ecriture " + error);

        std::string bytes;
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (f != nullptr) {
            char buf[4096];
            std::size_t n = 0;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.append(buf, n);
            std::fclose(f);
        }
        gw::doc::ZipReader zip;
        std::string content;
        if (zip.open(bytes, error) && zip.extract("content.xml", content, error)) {
            check(content.find("gw:") == std::string::npos,
                  "ODT sans style : aucun attribut gw: dans content.xml");
            check(content.find("<text:span") == std::string::npos,
                  "ODT sans style : aucun <text:span> dans content.xml");
        } else {
            check(false, "ODT sans style : relecture de content.xml " + error);
        }
    }

    // --- Detection dencodage TXT ---
    {
        const std::string path = dir + "/encodage.txt";

        // CP1252 : 0xE9 = e accent aigu, invalide en UTF-8 isole.
        const std::string cp1252 = "Cafe\xE9 et cr\xE8me";
        std::FILE* f = std::fopen(path.c_str(), "wb");
        std::fwrite(cp1252.data(), 1, cp1252.size(), f);
        std::fclose(f);

        gw::doc::Document d;
        const bool ok = gw::doc::load_txt(path, d, error);
        check(ok, "TXT CP1252 : lecture acceptee");
        // Transcode, le texte doit etre de lUTF-8 valide et plus long en octets.
        check(ok && d.paragraphs().size() == 1 &&
                  d.paragraphs()[0].text.size() == cp1252.size() + 2,
              "TXT CP1252 : transcode en UTF-8 (2 octets de plus)");

        // BOM UTF-8.
        const std::string bom_utf8 = std::string("\xEF\xBB\xBF") + "avec BOM";
        f = std::fopen(path.c_str(), "wb");
        std::fwrite(bom_utf8.data(), 1, bom_utf8.size(), f);
        std::fclose(f);
        gw::doc::Document d2;
        check(gw::doc::load_txt(path, d2, error) && d2.paragraphs().size() == 1 &&
                  d2.paragraphs()[0].text == "avec BOM",
              "TXT BOM UTF-8 : BOM retire");

        // BOM UTF-16 : doit etre refuse explicitement.
        const std::string bom_utf16 = std::string("\xFF\xFE", 2) + std::string("a\0b\0", 4);
        f = std::fopen(path.c_str(), "wb");
        std::fwrite(bom_utf16.data(), 1, bom_utf16.size(), f);
        std::fclose(f);
        gw::doc::Document d3;
        const bool refused = !gw::doc::load_txt(path, d3, error);
        check(refused, "TXT UTF-16 : refuse avec message clair");
        if (refused) std::printf("       message : %s\n", error.c_str());
    }

    // --- Fins de ligne ---
    {
        const std::string path = dir + "/eol.txt";
        const std::string mixed = "un\r\ndeux\ntrois\rquatre";
        std::FILE* f = std::fopen(path.c_str(), "wb");
        std::fwrite(mixed.data(), 1, mixed.size(), f);
        std::fclose(f);

        gw::doc::Document d;
        const bool ok = gw::doc::load_txt(path, d, error);
        check(ok && d.paragraphs().size() == 4, "TXT : CRLF, LF et CR isole acceptes");

        // Reecriture : doit produire du CRLF uniquement.
        gw::doc::save_txt(path, d, error);
        std::string out;
        f = std::fopen(path.c_str(), "rb");
        char buf[256];
        std::size_t n = std::fread(buf, 1, sizeof(buf), f);
        std::fclose(f);
        out.assign(buf, n);
        check(out == "un\r\ndeux\r\ntrois\r\nquatre", "TXT : ecriture normalisee en CRLF");
    }

    // --- .odt COMPRESSE, produit par un outil tiers -------------------------
    //
    // C est le test qui prouve que la limitation de lecture est levee : ce
    // fichier n a pas ete ecrit par nous et ses entrees sont en methode 8.
    // Sans le decodeur DEFLATE maison, il etait tout simplement illisible.
    {
        const std::string path = dir + "/external.odt";
        std::FILE* probe = std::fopen(path.c_str(), "rb");
        if (probe == nullptr) {
            std::printf("[SKIP]  external.odt absent, test du deflate non execute\n");
        } else {
            std::fclose(probe);
            gw::doc::Document external;
            const bool ok = gw::doc::load_odt(path, external, error);
            check(ok, "ODT externe compresse (methode 8) : lecture " +
                          (ok ? std::string() : error));
            if (ok) {
                check(external.paragraphs().size() > 5,
                      "ODT externe : " + std::to_string(external.paragraphs().size()) +
                          " paragraphes extraits");
                check(!external.paragraphs().empty() &&
                          external.paragraphs()[0].text.find("outil tiers") != std::string::npos,
                      "ODT externe : premier paragraphe correct");

                // Les entites XML doivent avoir ete decodees comme partout ailleurs.
                bool found_escaped = false;
                for (const auto& paragraph : external.paragraphs()) {
                    if (paragraph.text.find("<balise> & \"guillemets\"") !=
                        std::string::npos) {
                        found_escaped = true;
                    }
                }
                check(found_escaped, "ODT externe : entites XML decodees");
            }
        }
    }

    std::printf("\n%s (%d echec%s)\n", failures == 0 ? "TOUT PASSE" : "ECHECS", failures,
                failures > 1 ? "s" : "");
    return failures == 0 ? 0 : 1;
}
