#include "app/i18n.h"

#include <array>
#include <cstddef>

namespace gw::app::i18n {
namespace {

Language g_current_language = Language::French;

// Table bidimensionnelle [StringId][Language]
// Stockee de maniere compacte en memoire sans aucune allocation dynamique.
constexpr const char* kTranslations[static_cast<std::size_t>(StringId::Count)][static_cast<std::size_t>(Language::Count)] = {
    // MenuFile
    {"Fichier", "File", "Archivo", "Datei", "File", "Arquivo", "Bestand", "Plik", "Файл", "Dosya"},
    // MenuEdit
    {"Édition", "Edit", "Edición", "Bearbeiten", "Modifica", "Editar", "Bewerken", "Edycja", "Правка", "Düzen"},
    // MenuView
    {"Affichage", "View", "Ver", "Ansicht", "Visualizza", "Exibir", "Beeld", "Widok", "Вид", "Görünüm"},
    // ActionNew
    {"Nouveau", "New", "Nuevo", "Neu", "Nuovo", "Novo", "Nieuw", "Nowy", "Создать", "Yeni"},
    // ActionOpen
    {"Ouvrir…", "Open…", "Abrir…", "Öffnen…", "Apri…", "Abrir…", "Openen…", "Otwórz…", "Открыть…", "Aç…"},
    // ActionSave
    {"Enregistrer", "Save", "Guardar", "Speichern", "Salva", "Salvar", "Opslaan", "Zapisz", "Сохранить", "Kaydet"},
    // ActionSaveAs
    {"Enregistrer sous…", "Save As…", "Guardar como…", "Speichern unter…", "Salva con nome…", "Salvar como…", "Opslaan als…", "Zapisz jako…", "Сохранить как…", "Farklı Kaydet…"},
    // ActionHome
    {"Retour à l'accueil", "Back to Home", "Volver al inicio", "Zurück zur Startseite", "Torna alla home", "Voltar ao início", "Terug naar start", "Strona główna", "На главную", "Ana Sayfaya Dön"},
    // ActionUndo
    {"Annuler", "Undo", "Deshacer", "Rückgängig", "Annulla", "Desfazer", "Ongedaan maken", "Cofnij", "Отменить", "Geri Al"},
    // ActionRedo
    {"Rétablir", "Redo", "Rehacer", "Wiederholen", "Ripeti", "Refazer", "Opnieuw", "Ponów", "Повторить", "Yinele"},
    // ActionCut
    {"Couper", "Cut", "Cortar", "Ausschneiden", "Taglia", "Recortar", "Knippen", "Wytnij", "Вырезать", "Kes"},
    // ActionCopy
    {"Copier", "Copy", "Copiar", "Kopieren", "Copia", "Copiar", "Kopiëren", "Kopiuj", "Копировать", "Kopyala"},
    // ActionPaste
    {"Coller", "Paste", "Pegar", "Einfügen", "Incolla", "Colar", "Plakken", "Wklej", "Вставить", "Yapıştır"},
    // ActionSelectAll
    {"Sélectionner tout", "Select All", "Seleccionar todo", "Alles auswählen", "Seleziona tutto", "Selecionar tudo", "Alles selecteren", "Zaznacz wszystko", "Выделить всё", "Tümünü Seç"},
    // ActionToggleDark
    {"Mode sombre", "Dark mode", "Modo oscuro", "Dunkelmodus", "Modalità scura", "Modo escuro", "Donkere modus", "Tryb ciemny", "Тёмный режим", "Karanlık mod"},
    // ActionToggleKeyboard
    {"Mode clavier (Markdown live)", "Keyboard mode (Markdown live)", "Modo teclado (Markdown live)", "Tastaturmodus (Markdown live)", "Modalità tastiera (Markdown live)", "Modo teclado (Markdown live)", "Toetsenbordmodus (Markdown live)", "Tryb klawiatury (Markdown live)", "Режим клавиатуры (Markdown live)", "Klavye modu (Markdown live)"},

    // ActionInsertTable
    {"Insérer un tableau", "Insert table", "Insertar tabla", "Tabelle einfügen", "Inserisci tabella", "Inserir tabela", "Tabel invoegen", "Wstaw tabelę", "Вставить таблицу", "Tablo ekle"},
    // ActionDeleteTable
    {"Supprimer le tableau", "Delete table", "Eliminar tabla", "Tabelle löschen", "Elimina tabella", "Excluir tabela", "Tabel verwijderen", "Usuń tabelę", "Удалить таблицу", "Tabloyu sil"},

    // HomeTagline
    {"L'écriture, sans fard.", "Writing, unadorned.", "La escritura, sin artificios.", "Schreiben, unverfälscht.", "La scrittura, senza orpelli.", "A escrita, sem artifícios.", "Schrijven, ongekleed.", "Pisanie, bez zbędnych ozdób.", "Письмо, без прикрас.", "Yazı, süssüz."},
    // HomeNewNote
    {"Nouvelle note", "New note", "Nueva nota", "Neue Notiz", "Nuova nota", "Nova nota", "Nieuwe notitie", "Nowa notatka", "Новая заметка", "Yeni not"},
    // HomeOpenNote
    {"Ouvrir un fichier", "Open a file", "Abrir un archivo", "Datei öffnen", "Apri un file", "Abrir um arquivo", "Bestand openen", "Otwórz plik", "Открыть файл", "Dosya aç"},
    // HomeSettings
    {"Paramètres", "Settings", "Configuración", "Einstellungen", "Impostazioni", "Configurações", "Instellingen", "Ustawienia", "Настройки", "Ayarlar"},
    // HomeRecent
    {"Notes récentes", "Recent notes", "Notas recientes", "Zuletzt geöffnet", "Note recenti", "Notas recentes", "Recente notities", "Ostatnie notatki", "Недавние заметки", "Son notlar"},
    // HomeNoRecent
    {"Aucune note récente", "No recent notes", "No hay notas recientes", "Keine letzten Notizen", "Nessuna nota recente", "Nenhuma nota recente", "Geen recente notities", "Brak ostatnich notatek", "Нет недавних заметок", "Son not yok"},
    // HomeUntitled
    {"Note sans titre", "Untitled note", "Nota sin título", "Unbenannte Notiz", "Nota senza titolo", "Nota sem título", "Naamloze notitie", "Notatka bez tytułu", "Безымянная заметка", "Başlıksız not"},

    // SettingsTitle
    {"Paramètres", "Settings", "Configuración", "Einstellungen", "Impostazioni", "Configurações", "Instellingen", "Ustawienia", "Настройки", "Ayarlar"},
    // SettingsAppearance
    {"Apparence", "Appearance", "Apariencia", "Erscheinungsbild", "Aspetto", "Aparência", "Uiterlijk", "Wygląd", "Внешний вид", "Görünüm"},
    // SettingsDarkMode
    {"Mode sombre", "Dark mode", "Modo oscuro", "Dunkelmodus", "Modalità scura", "Modo escuro", "Donkere modus", "Tryb ciemny", "Тёмный режим", "Karanlık mod"},
    // SettingsFontSize
    {"Taille de police par défaut", "Default font size", "Tamaño de fuente por defecto", "Standard-Schriftgröße", "Dimensione carattere predefinita", "Tamanho de fonte padrão", "Standaardlettergrootte", "Domyślny rozmiar czcionki", "Размер шрифта по умолчанию", "Varsayılan yazı boyutu"},
    // SettingsFonts
    {"Polices", "Fonts", "Fuentes", "Schriftarten", "Caratteri", "Fontes", "Lettertypen", "Czcionki", "Шрифты", "Yazı Tipleri"},
    // SettingsImportFont
    {"Importer un fichier .ttf…", "Import a .ttf file…", "Importar archivo .ttf…", ".ttf-Datei importieren…", "Importa un file .ttf…", "Importar arquivo .ttf…", ".ttf-bestand importeren…", "Importuj plik .ttf…", "Импортировать файл .ttf…", ".ttf dosyası içe aktar…"},
    // SettingsStartup
    {"Au démarrage", "At startup", "Al inicio", "Beim Start", "All'avvio", "Na inicialização", "Bij opstarten", "Przy uruchamianiu", "При запуске", "Başlangıçta"},
    // SettingsOpenLast
    {"Aller dans la dernière note ouverte", "Open last opened note", "Abrir la última nota abierta", "Zuletzt geöffnete Notiz öffnen", "Apri l'ultima nota aperta", "Abrir a última nota aberta", "Laatst geopende notitie openen", "Otwórz ostatnią notatkę", "Открывать последнюю заметку", "Son açılan notu aç"},
    // SettingsShowQuote
    {"Afficher la citation du jour", "Show quote of the day", "Mostrar la cita del día", "Zitat des Tages anzeigen", "Mostra citazione del giorno", "Exibir citação do dia", "Citaat van de dag weergeven", "Pokaż cytat dnia", "Показывать цитату дня", "Günün sözünü göster"},
    // SettingsQuoteDuration
    {"Durée d'affichage :", "Display duration:", "Duración de visualización:", "Anzeigedauer:", "Durata di visualizzazione:", "Duração de exibição:", "Weergaveduur:", "Czas wyświetlania:", "Время показа:", "Görüntüleme süresi:"},
    // SettingsQuoteDurLoading
    {"Au chargement", "While loading", "Al cargar", "Beim Laden", "Al caricamento", "Ao carregar", "Tijdens laden", "Podczas ładowania", "При загрузке", "Yüklenirken"},
    // SettingsQuoteDur5s
    {"5 secondes", "5 seconds", "5 segundos", "5 Sekunden", "5 secondi", "5 segundos", "5 seconden", "5 sekund", "5 секунд", "5 saniye"},
    // SettingsQuoteDur15s
    {"15 secondes", "15 seconds", "15 segundos", "15 Sekunden", "15 secondi", "15 segundos", "15 seconden", "15 sekund", "15 секунд", "15 saniye"},
    // SettingsLanguage
    {"Langue", "Language", "Idioma", "Sprache", "Lingua", "Idioma", "Taal", "Język", "Язык", "Dil"},
    // SettingsSmoothCaret
    {"Animation fluide du curseur (style Word)", "Smooth cursor animation (Word style)", "Animación fluida del cursor (estilo Word)", "Flüssige Cursor-Animation (Word-Stil)", "Animazione fluida del cursore (stile Word)", "Animação suave do cursor (estilo Word)", "Vloeiende cursoranimatie (Word-stijl)", "Płynna animacja kursora (styl Word)", "Плавная анимация курсора (стиль Word)", "Akıcı imleç animasyonu (Word stili)"},

    // StatusBackend
    {"Backend : ", "Backend: ", "Motor: ", "Backend: ", "Backend: ", "Backend: ", "Backend: ", "Silnik: ", "Движок: ", "Backend: "},
    // StatusShortcuts
    {"Ctrl+S : enregistrer  |  Ctrl+B/I/U : mise en forme  |  Échap : accueil",
     "Ctrl+S: save  |  Ctrl+B/I/U: formatting  |  Esc: home",
     "Ctrl+S: guardar  |  Ctrl+B/I/U: formato  |  Esc: inicio",
     "Strg+S: speichern  |  Strg+B/I/U: Formatierung  |  Esc: Startseite",
     "Ctrl+S: salva  |  Ctrl+B/I/U: formattazione  |  Esc: home",
     "Ctrl+S: salvar  |  Ctrl+B/I/U: formatação  |  Esc: início",
     "Ctrl+S: opslaan  |  Ctrl+B/I/U: opmaak  |  Esc: start",
     "Ctrl+S: zapisz  |  Ctrl+B/I/U: formatowanie  |  Esc: start",
     "Ctrl+S: сохранить  |  Ctrl+B/I/U: форматирование  |  Esc: главная",
     "Ctrl+S: kaydet  |  Ctrl+B/I/U: biçimlendirme  |  Esc: ana sayfa"},
    // ConfirmDiscardTitle
    {"Gastinel Write", "Gastinel Write", "Gastinel Write", "Gastinel Write", "Gastinel Write", "Gastinel Write", "Gastinel Write", "Gastinel Write", "Gastinel Write", "Gastinel Write"},
    // ConfirmDiscardMessage
    {"Enregistrer les modifications avant de continuer ?",
     "Save changes before continuing?",
     "¿Guardar los cambios antes de continuar?",
     "Änderungen vor dem Fortfahren speichern?",
     "Salvare le modifiche prima di continuare?",
     "Salvar as alterações antes de continuar?",
     "Wijzigingen opslaan voor vertrek?",
     "Zapisać zmiany przed kontynuowaniem?",
     "Сохранить изменения перед выходом?",
     "Devam etmeden önce değişiklikler kaydedilsin mi?"},
    // UntitledDoc
    {"Nouvelle note", "New note", "Nueva nota", "Neue Notiz", "Nuova nota", "Nova nota", "Nieuwe notitie", "Nowa notatka", "Новая заметка", "Yeni not"},
    // AppTitle
    {"Gastinel Write", "Gastinel Write", "Gastinel Write", "Gastinel Write", "Gastinel Write", "Gastinel Write", "Gastinel Write", "Gastinel Write", "Gastinel Write", "Gastinel Write"},
};

const std::vector<LanguageEntry> kLanguages = {
    {Language::French, "fr", "Français"},
    {Language::English, "en", "English"},
    {Language::Spanish, "es", "Español"},
    {Language::German, "de", "Deutsch"},
    {Language::Italian, "it", "Italiano"},
    {Language::Portuguese, "pt", "Português"},
    {Language::Dutch, "nl", "Nederlands"},
    {Language::Polish, "pl", "Polski"},
    {Language::Russian, "ru", "Русский"},
    {Language::Turkish, "tr", "Türkçe"},
};

} // namespace

const char* tr(StringId id) {
    return tr(id, g_current_language);
}

const char* tr(StringId id, Language lang) {
    const auto sid = static_cast<std::size_t>(id);
    const auto lid = static_cast<std::size_t>(lang);
    if (sid >= static_cast<std::size_t>(StringId::Count)) return "";
    if (lid >= static_cast<std::size_t>(Language::Count)) return kTranslations[sid][0];
    const char* str = kTranslations[sid][lid];
    return str != nullptr ? str : "";
}

Language current_language() {
    return g_current_language;
}

void set_language(Language lang) {
    if (static_cast<std::size_t>(lang) < static_cast<std::size_t>(Language::Count)) {
        g_current_language = lang;
    }
}

const char* language_code(Language lang) {
    for (const auto& entry : kLanguages) {
        if (entry.lang == lang) return entry.code;
    }
    return "fr";
}

const char* language_name(Language lang) {
    for (const auto& entry : kLanguages) {
        if (entry.lang == lang) return entry.name;
    }
    return "Français";
}

Language language_from_code(std::string_view code) {
    for (const auto& entry : kLanguages) {
        if (entry.code == code) return entry.lang;
    }
    return Language::French;
}

const std::vector<LanguageEntry>& all_languages() {
    return kLanguages;
}

} // namespace gw::app::i18n
