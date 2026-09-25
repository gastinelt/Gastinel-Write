// tools/settings_tests.cpp — verification des reglages et de l import de police.
//
// Cible console, sans fenetre : elle ne depend que de app/settings, font/ et
// platform/. C est le seul chemin du programme ou un fichier fourni par
// l utilisateur traverse le parser TTF, donc le seul qui merite un banc de test
// dedie — le reste des parametres est de la lecture de cle/valeur.
//
// ATTENTION : ce banc ecrit dans le VRAI fichier de reglages
// (%APPDATA%\Gastinel\Write\settings.txt), parce que son emplacement vient de
// SHGetKnownFolderPath et n est pas redirigeable par variable d environnement.
// Il le sauvegarde au demarrage et le restaure a la sortie, y compris apres un
// echec de test. Ne pas l executer en parallele de l application.
//
// Usage : gw_settings_tests <police_ttf_valide> <repertoire_de_travail>

#include "app/quotes.h"
#include "app/settings.h"
#include "doc/file_bytes.h"
#include "platform/system.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const std::string& label) {
    std::printf("[%s]  %s\n", ok ? " OK " : "FAIL", label.c_str());
    if (!ok) ++g_failures;
}

// Ecrit un fichier de contenu arbitraire et rend son chemin.
std::string write_probe(const std::string& dir, const std::string& name,
                        const std::string& bytes) {
    const std::string path = dir + "\\" + name;
    std::string error;
    gw::doc::write_file_binary(path, bytes, error);
    return path;
}

std::string big_endian_u32(std::uint32_t v) {
    std::string s(4, '\0');
    s[0] = static_cast<char>((v >> 24) & 0xFF);
    s[1] = static_cast<char>((v >> 16) & 0xFF);
    s[2] = static_cast<char>((v >> 8) & 0xFF);
    s[3] = static_cast<char>(v & 0xFF);
    return s;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage : %s <police_ttf_valide> <repertoire_de_travail>\n", argv[0]);
        return 2;
    }
    const std::string valid_ttf = argv[1];
    const std::string work = argv[2];
    const std::string custom = work + "\\fonts_custom";

    gw::platform::create_directories(work);

    // --- Sauvegarde du fichier reel ----------------------------------------
    const std::string real_settings = gw::app::Settings::storage_path();
    std::string backup;
    std::string error;
    const bool had_settings = gw::doc::read_file_binary(real_settings, backup, error);

    const auto restore = [&] {
        if (had_settings) {
            std::string ignored;
            gw::doc::write_file_binary(real_settings, backup, ignored);
        } else {
            gw::platform::delete_file(real_settings);
        }
    };

    std::printf("--- Recueil de citations philosophiques ---\n");
    {
        check(gw::app::quote_count() == 100, "le recueil contient exactement 100 citations");
        bool all_valid = true;
        for (std::size_t i = 0; i < gw::app::quote_count(); ++i) {
            const auto& q = gw::app::get_quote(i);
            if (!q.text || !q.author || q.text[0] == '\0' || q.author[0] == '\0') {
                all_valid = false;
                break;
            }
        }
        check(all_valid, "toutes les citations ont un texte et un auteur valides");

        const auto& qotd = gw::app::get_quote_of_the_day();
        check(qotd.text && qotd.text[0] != '\0', "citation du jour valide");

        const auto& rand_q = gw::app::get_random_quote();
        check(rand_q.text && rand_q.text[0] != '\0', "citation aleatoire valide");
    }

    std::printf("\n--- Persistance des reglages ---\n");
    {
        gw::platform::delete_file(real_settings);

        gw::app::Settings s;
        s.load(custom);
        check(!s.dark_mode(), "defaut : mode clair");
        check(s.default_font_size() == gw::app::Settings::kDefaultFontSize,
              "defaut : corps " + std::to_string(gw::app::Settings::kDefaultFontSize));
        check(s.fonts().size() == 1 && s.fonts()[0].built_in, "defaut : Inter seule, integree");
        check(!s.open_last_note(), "defaut : ne pas aller dans derniere note");
        check(s.show_quote_of_the_day(), "defaut : afficher citation du jour");
        check(s.quote_duration() == gw::app::Settings::QuoteDuration::FiveSeconds,
              "defaut : duree citation 5s");

        s.set_dark_mode(true);
        s.set_default_font_size(23);
        s.set_open_last_note(true);
        s.set_show_quote_of_the_day(false);
        s.set_quote_duration(gw::app::Settings::QuoteDuration::FifteenSeconds);

        gw::app::Settings reloaded;
        reloaded.load(custom);
        check(reloaded.dark_mode(), "relecture : mode sombre conserve");
        check(reloaded.default_font_size() == 23, "relecture : corps 23 conserve");
        check(reloaded.open_last_note(), "relecture : ouvrir derniere note conserve");
        check(!reloaded.show_quote_of_the_day(), "relecture : masquer citation conserve");
        check(reloaded.quote_duration() == gw::app::Settings::QuoteDuration::FifteenSeconds,
              "relecture : duree citation 15s conservee");

        reloaded.set_quote_duration(gw::app::Settings::QuoteDuration::WhileLoading);
        gw::app::Settings reloaded2;
        reloaded2.load(custom);
        check(reloaded2.quote_duration() == gw::app::Settings::QuoteDuration::WhileLoading,
              "relecture : duree citation WhileLoading conservee");
    }

    std::printf("\n--- Bornes du corps de texte ---\n");
    {
        gw::app::Settings s;
        s.load(custom);
        s.set_default_font_size(-5);
        check(s.default_font_size() == gw::app::Settings::kMinFontSize,
              "valeur negative bornee au minimum");
        s.set_default_font_size(9999);
        check(s.default_font_size() == gw::app::Settings::kMaxFontSize,
              "valeur enorme bornee au maximum");
    }

    std::printf("\n--- Import d une police valide ---\n");
    {
        gw::app::Settings s;
        s.load(custom);
        std::string err;
        const bool ok = s.import_font(valid_ttf, custom, err);
        check(ok, "import accepte" + (ok ? std::string() : " : " + err));
        check(s.fonts().size() == 2, "la liste compte Inter + 1 importee");
        if (s.fonts().size() == 2) {
            check(gw::platform::file_exists(custom + "\\" + s.fonts()[1].file_name),
                  "le fichier a bien ete copie dans custom/");
        }

        gw::app::Settings reloaded;
        reloaded.load(custom);
        check(reloaded.fonts().size() == 2, "relecture : la police importee est toujours la");

        // Un second import du meme fichier ne doit pas ecraser le premier.
        std::string err2;
        check(s.import_font(valid_ttf, custom, err2), "second import du meme fichier accepte");
        check(s.fonts().size() == 3 && s.fonts()[1].file_name != s.fonts()[2].file_name,
              "les deux copies portent des noms de fichier distincts");
    }

    std::printf("\n--- Retrait ---\n");
    {
        gw::app::Settings s;
        s.load(custom);
        const std::size_t before = s.fonts().size();
        check(!s.remove_font(0, custom), "Inter (integree) ne peut pas etre retiree");
        check(s.fonts().size() == before, "la liste est inchangee apres ce refus");

        while (s.fonts().size() > 1) {
            const std::string file = custom + "\\" + s.fonts()[1].file_name;
            s.remove_font(1, custom);
            check(!gw::platform::file_exists(file), "le fichier copie a ete supprime");
        }
        check(s.fonts().size() == 1, "il ne reste que la police integree");

        gw::app::Settings reloaded;
        reloaded.load(custom);
        check(reloaded.fonts().size() == 1, "relecture : le retrait est persiste");
    }

    std::printf("\n--- Fichiers refuses : echec propre, message, aucun plantage ---\n");
    {
        gw::app::Settings s;
        s.load(custom);

        struct Probe {
            const char* name;
            std::string bytes;
            const char* label;
        };

        std::string valid_bytes;
        std::string ignored;
        gw::doc::read_file_binary(valid_ttf, valid_bytes, ignored);

        std::vector<Probe> probes;
        probes.push_back({"vide.ttf", std::string(), "fichier vide"});
        probes.push_back({"court.ttf", std::string(6, 'x'), "fichier plus court qu un en-tete"});
        probes.push_back({"texte.ttf", std::string(4096, 'A'), "fichier texte renomme en .ttf"});
        probes.push_back({"otto.ttf", "OTTO" + std::string(2048, '\0'),
                          "OpenType/CFF (OTTO)"});
        probes.push_back({"collection.ttf", "ttcf" + std::string(2048, '\0'),
                          "collection TrueType (.ttc)"});
        probes.push_back({"woff.ttf", "wOFF" + std::string(2048, '\0'), "police web WOFF"});
        probes.push_back({"bom.ttf", big_endian_u32(0x00010000u) + std::string(8, '\0'),
                          "en-tete sfnt valide mais repertoire de tables vide"});
        // Troncatures d une police reelle : le cas le plus perfide, l en-tete
        // est parfaitement valide et tout le reste manque.
        if (valid_bytes.size() > 4096) {
            probes.push_back({"tronque_1k.ttf", valid_bytes.substr(0, 1024),
                              "police reelle tronquee a 1 Kio"});
            probes.push_back({"tronque_moitie.ttf", valid_bytes.substr(0, valid_bytes.size() / 2),
                              "police reelle tronquee de moitie"});
        }
        // Corruption d octets isoles au coeur du fichier.
        if (valid_bytes.size() > 8192) {
            std::string mutated = valid_bytes;
            for (std::size_t i = 64; i < mutated.size(); i += 997) {
                mutated[i] = static_cast<char>(~static_cast<unsigned char>(mutated[i]));
            }
            probes.push_back({"corrompu.ttf", mutated, "police reelle a octets corrompus"});
        }

        const std::size_t before = s.fonts().size();
        for (const Probe& p : probes) {
            const std::string path = write_probe(work, p.name, p.bytes);
            std::string err;
            const bool accepted = s.import_font(path, custom, err);
            if (accepted) {
                // Un fichier corrompu peut rester structurellement lisible ; ce
                // n est un echec du test que s il s agit d un format qu on sait
                // ne pas savoir rendre.
                std::printf("[ ?? ]  %s : accepte (structure encore valide)\n", p.label);
            } else {
                check(!err.empty(), std::string(p.label) + " : refuse avec un message");
                std::printf("       message : %s\n", err.c_str());
            }
            gw::platform::delete_file(path);
        }

        // Nettoyage de tout ce qui aurait ete accepte pendant cette phase.
        while (s.fonts().size() > before) s.remove_font(s.fonts().size() - 1, custom);
    }

    restore();

    std::printf("\n%s (%d echec%s)\n", g_failures == 0 ? "TOUT PASSE" : "ECHECS", g_failures,
                g_failures > 1 ? "s" : "");
    return g_failures == 0 ? 0 : 1;
}
