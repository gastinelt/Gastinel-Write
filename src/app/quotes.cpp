// app/quotes.cpp — recueil de 100 citations philosophiques.
#include "app/quotes.h"

#include <array>
#include <chrono>
#include <random>

namespace gw::app {
namespace {

const std::array<Quote, 100> kQuotes = {{
    {"Connais-toi toi-même.", "Socrate"},
    {"Une vie sans examen ne vaut pas d'être vécue.", "Socrate"},
    {"Tout ce que je sais, c'est que je ne sais rien.", "Socrate"},
    {"L'étonnement est la racine de la philosophie.", "Platon"},
    {"On ne peut pas descendre deux fois dans le même fleuve.", "Héraclite"},
    {"L'espérance est un songe éveillé.", "Aristote"},
    {"L'excellence est un art que l'on n'atteint que par l'exercice constant.", "Aristote"},
    {"Le sage ne cherche pas le plaisir, mais l'absence de douleur.", "Aristote"},
    {"Le plaisir est le commencement et la fin de la vie heureuse.", "Épicure"},
    {"Ce qui trouble les hommes, ce ne sont pas les choses, mais les jugements qu'ils portent sur elles.", "Épictète"},
    {"N'attends pas que les événements arrivent comme tu le souhaites ; décide de vouloir ce qui arrive.", "Épictète"},
    {"Nul n'est libre qui n'est maître de lui-même.", "Épictète"},
    {"Que la force me soit donnée de supporter ce qui ne peut être changé et le courage de changer ce qui peut l'être.", "Marc Aurèle"},
    {"Notre vie est ce que nos pensées en font.", "Marc Aurèle"},
    {"Regarde en toi-même : c'est là qu'est la source du bien.", "Marc Aurèle"},
    {"Tout est éphémère, le souvenir comme l'objet du souvenir.", "Marc Aurèle"},
    {"Ce n'est pas parce que les choses sont difficiles que nous n'osons pas, c'est parce que nous n'osons pas qu'elles sont difficiles.", "Sénèque"},
    {"Hâte-toi de bien vivre et songe que chaque jour est à lui seul une vie.", "Sénèque"},
    {"Il n'y a point de vent favorable pour celui qui ne sait où il va.", "Sénèque"},
    {"La vie, ce n'est pas d'attendre que les orages passent, c'est d'apprendre à danser sous la pluie.", "Sénèque"},
    {"Le temps guérit ce que la raison n'a pas su guérir.", "Sénèque"},
    {"Le plus sage est celui qui sait qu'il ne sait pas.", "Platon"},
    {"La simplicité est le comble de la sophistication.", "Léonard de Vinci"},
    {"Chaque homme porte la forme entière de l'humaine condition.", "Michel de Montaigne"},
    {"Que sais-je ?", "Michel de Montaigne"},
    {"Mieux vaut une tête bien faite qu'une tête bien pleine.", "Michel de Montaigne"},
    {"La plus grande chose du monde, c'est de savoir être à soi.", "Michel de Montaigne"},
    {"Philosopher, c'est apprendre à mourir.", "Michel de Montaigne"},
    {"Je pense, donc je suis.", "René Descartes"},
    {"Il n'y a rien qui soit entièrement en notre pouvoir, sinon nos pensées.", "René Descartes"},
    {"Le bon sens est la chose du monde la mieux partagée.", "René Descartes"},
    {"La lecture de tous les bons livres est comme une conversation avec les plus honnêtes gens des siècles passés.", "René Descartes"},
    {"Le cœur a ses raisons que la raison ne connaît point.", "Blaise Pascal"},
    {"L'homme n'est qu'un roseau, le plus faible de la nature ; mais c'est un roseau pensant.", "Blaise Pascal"},
    {"Tout le malheur des hommes vient d'une seule chose, qui est de ne savoir pas demeurer en repos, dans une chambre.", "Blaise Pascal"},
    {"La vraie morale se moque de la morale.", "Blaise Pascal"},
    {"La paix n'est pas l'absence de guerre, c'est une vertu qui naît de la force de l'âme.", "Baruch Spinoza"},
    {"Ne pas railler, ne pas déplorer, ne pas détester, mais comprendre.", "Baruch Spinoza"},
    {"La liberté est la conscience de la nécessité.", "Baruch Spinoza"},
    {"La félicité n'est pas la récompense de la vertu, mais la vertu elle-même.", "Baruch Spinoza"},
    {"J'ai décidé d'être heureux parce que c'est bon pour la santé.", "Voltaire"},
    {"Le doute est un état mental désagréable, mais la certitude est ridicule.", "Voltaire"},
    {"Il faut cultiver notre jardin.", "Voltaire"},
    {"L'homme est né libre, et partout il est dans les fers.", "Jean-Jacques Rousseau"},
    {"Le plus fort n'est jamais assez fort pour être toujours le maître, s'il ne transforme sa force en droit.", "Jean-Jacques Rousseau"},
    {"La patience est amère, mais son fruit est doux.", "Jean-Jacques Rousseau"},
    {"Sapere aude ! Aie le courage de te servir de ton propre entendement !", "Emmanuel Kant"},
    {"Deux choses remplissent le cœur d'admiration : le ciel étoilé au-dessus de moi et la loi morale en moi.", "Emmanuel Kant"},
    {"Agis de telle sorte que la maxime de ta volonté puisse être érigée en loi universelle.", "Emmanuel Kant"},
    {"On mesure l'intelligence d'un individu à la quantité d'incertitudes qu'il est capable de supporter.", "Emmanuel Kant"},
    {"Rien de grand ne s'est accompli dans le monde sans passion.", "G. W. F. Hegel"},
    {"La chouette de Minerve ne prend son envol qu'au crépuscule.", "G. W. F. Hegel"},
    {"L'homme n'est rien d'autre que la série de ses actes.", "G. W. F. Hegel"},
    {"Le bonheur n'est pas chose aisée : il est très difficile de le trouver en nous, et impossible de le trouver ailleurs.", "Arthur Schopenhauer"},
    {"La vie oscille, comme un pendule, de droite à gauche, de la souffrance à l'ennui.", "Arthur Schopenhauer"},
    {"Chaque journée est une petite vie.", "Arthur Schopenhauer"},
    {"Ce qui ne me tue pas me rend plus fort.", "Friedrich Nietzsche"},
    {"Deviens ce que tu es.", "Friedrich Nietzsche"},
    {"Sans la musique, la vie serait une erreur.", "Friedrich Nietzsche"},
    {"Il faut porter encore en soi un chaos pour pouvoir mettre au monde une étoile dansante.", "Friedrich Nietzsche"},
    {"Ce n'est pas le doute, c'est la certitude qui rend fou.", "Friedrich Nietzsche"},
    {"La vie n'a de sens que si on lui en donne un par ses actes.", "Jean-Paul Sartre"},
    {"L'existence précède l'essence.", "Jean-Paul Sartre"},
    {"Nous sommes condamnés à être libres.", "Jean-Paul Sartre"},
    {"Au milieu de l'hiver, j'ai découvert en moi un invincible été.", "Albert Camus"},
    {"Il faut imaginer Sisyphe heureux.", "Albert Camus"},
    {"Créer, c'est vivre deux fois.", "Albert Camus"},
    {"Mal nommer un objet, c'est ajouter au malheur de ce monde.", "Albert Camus"},
    {"L'attention est la forme la plus rare et la plus pure de la générosité.", "Simone Weil"},
    {"Le beau est la seule finalité ici-bas.", "Simone Weil"},
    {"Ne pas avoir de but est le propre des choses libres.", "Emil Cioran"},
    {"Un livre est une blessure différée.", "Emil Cioran"},
    {"Penser, c'est dire non.", "Alain"},
    {"L'action est le remède universel à l'inquiétude.", "Alain"},
    {"Le secret de l'action, c'est de s'y mettre.", "Alain"},
    {"L'intelligence consiste à comprendre qu'on n'a pas encore compris.", "Henri Bergson"},
    {"L'avenir n'est pas ce qui va arriver, mais ce que nous allons faire.", "Henri Bergson"},
    {"L'œil ne voit que ce que l'esprit est préparé à comprendre.", "Henri Bergson"},
    {"Le grand pardon est celui des offenses qui ne peuvent se pardonner.", "Vladimir Jankélévitch"},
    {"Il n'y a de pensée que là où il y a liberté.", "Hannah Arendt"},
    {"Penser et être pleinement vivant ne font qu'un.", "Hannah Arendt"},
    {"Le voyage de mille lieues commence par un premier pas.", "Lao Tseu"},
    {"Celui qui sait se contenter de ce qu'il a est riche.", "Lao Tseu"},
    {"Connaître les autres, c'est sagesse. Se connaître soi-même, c'est illumination.", "Lao Tseu"},
    {"La vie est vraiment simple, mais nous insistons à la rendre compliquée.", "Confucius"},
    {"Exige beaucoup de toi-même et attends peu des autres.", "Confucius"},
    {"Le silence est un ami qui ne trahit jamais.", "Confucius"},
    {"Celui qui déplace la montagne commence par déplacer de petites pierres.", "Confucius"},
    {"La vraie philosophie consiste à réapprendre à voir le monde.", "Maurice Merleau-Ponty"},
    {"Commencer est déjà la moitié du travail.", "Horace"},
    {"Carpe diem : cueille le jour présent sans te fier au lendemain.", "Horace"},
    {"Rien ne naît de rien.", "Lucrèce"},
    {"La mesure de l'amour, c'est d'aimer sans mesure.", "Saint Augustin"},
    {"Je ne cherche point à comprendre pour croire, mais je crois pour comprendre.", "Saint Anselme"},
    {"Tout passe, rien ne demeure.", "Héraclite"},
    {"Le caractère d'un homme est son destin.", "Héraclite"},
    {"La vérité est la fille du temps, non de l'autorité.", "Francis Bacon"},
    {"Savoir, c'est pouvoir.", "Francis Bacon"},
    {"L'homme est un loup pour l'homme, si l'on ne se fie qu'à la nature.", "Thomas Hobbes"},
    {"La beauté des choses réside dans l'esprit de celui qui les contemple.", "David Hume"}
}};

} // namespace

std::size_t quote_count() { return kQuotes.size(); }

const Quote& get_quote(std::size_t index) {
    if (index >= kQuotes.size()) index = index % kQuotes.size();
    return kQuotes[index];
}

const Quote& get_quote_of_the_day() {
    // Calcul deterministe base sur le jour calendrier (nombre de jours depuis l'epoque)
    const auto now = std::chrono::system_clock::now();
    const auto days = std::chrono::duration_cast<std::chrono::hours>(now.time_since_epoch()).count() / 24;
    const std::size_t index = static_cast<std::size_t>(days >= 0 ? days : -days) % kQuotes.size();
    return kQuotes[index];
}

const Quote& get_random_quote() {
    static std::mt19937 rng(static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<std::size_t> dist(0, kQuotes.size() - 1);
    return kQuotes[dist(rng)];
}

} // namespace gw::app
