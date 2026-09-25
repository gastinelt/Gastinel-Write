cmake_minimum_required(VERSION 3.20)
project(GastinelWrite LANGUAGES CXX)

# Cible : Windows 10/11 x86-64 uniquement. Aucune dependance externe :
# ni gestionnaire de paquets, ni bibliotheque tierce. Tout ce qui est lie ici
# fait partie du SDK Windows.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Lien statique du runtime de la chaine de compilation --------------------
# Avec MinGW, le binaire importe par defaut libstdc++-6.dll, libgcc_s_seh-1.dll
# et libwinpthread-1.dll, qui ne sont presentes que sur une machine ou la
# toolchain est installee. Un executable a copier tel quel ne peut pas dependre
# de cela : ces trois bibliotheques sont liees statiquement.
#
# `-static` couvre aussi libwinpthread, que -static-libstdc++ seul laisserait en
# import dynamique. Le compromis est un binaire plus lourd (cf. README).
# Applique globalement pour que les cibles de verification soient aussi
# autonomes que le binaire applicatif.
#
# A noter : cela ne touche pas les api-ms-win-crt-*.dll (UCRT), qui font partie
# du systeme et non de la toolchain - le plancher Windows reste inchange.
if(NOT MSVC)
    add_link_options(-static-libgcc -static-libstdc++ -static)
endif()

add_executable(gastinel_write WIN32
    src/app/main.cpp
    src/app/app.cpp
    src/app/chrome.cpp
    src/app/context_menu.cpp
    src/app/editor_view.cpp
    src/app/format_bar.cpp
    src/app/fonts.cpp
    src/app/quotes.cpp
    src/app/splash.cpp
    src/app/home_screen.cpp
    src/app/menu_bar.cpp
    src/app/recent_files.cpp
    src/app/settings.cpp
    src/app/settings_screen.cpp
    src/app/theme.cpp
    src/app/i18n.cpp

    src/platform/window.cpp
    src/platform/backend.cpp
    src/platform/gl_backend.cpp
    src/platform/gdi_backend.cpp
    src/platform/log.cpp
    src/platform/clipboard.cpp
    src/platform/dialogs.cpp
    src/platform/entry.cpp
    src/platform/popup_window.cpp
    src/platform/system.cpp
    src/platform/unicode.cpp

    src/render/rasterizer.cpp
    src/render/glyph_atlas.cpp
    src/render/draw.cpp
    src/render/icons.cpp
    src/render/text_run.cpp

    src/font/ttf.cpp

    src/compress/checksum.cpp
    src/compress/inflate.cpp
    src/compress/compress_lz.cpp

    src/image/png.cpp

    src/doc/txt_io.cpp
    src/doc/odt_io.cpp
    src/doc/text_edit.cpp
    src/doc/style.cpp
    src/doc/markdown_live.cpp
    src/doc/zip.cpp
    src/doc/file_bytes.cpp
)

# Les includes sont ecrits en chemin complet depuis src/ (ex. "font/ttf.h"),
# ce qui rend l origine de chaque en-tete lisible d un coup d oeil.
target_include_directories(gastinel_write PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# opengl32 : contexte WGL. gdi32/user32 : fenetrage et repli logiciel.
# shell32 : SHAppBarMessage, pour detecter une barre des taches en masquage
# automatique quand la fenetre sans chrome est maximisee.
# dwmapi nest volontairement PAS lie : le seul appel DWM (coins carres sous
# Windows 11) est resolu dynamiquement, pour ne pas exiger sa presence.
# comdlg32 : selecteur de fichier natif. ole32 : SHGetKnownFolderPath et
# CoTaskMemFree, pour localiser %APPDATA%.
# gdiplus nest ni lie ni charge : le decodage PNG est passe au decodeur maison
# de image/, qui ne depend daucune API systeme.
target_link_libraries(gastinel_write PRIVATE opengl32 gdi32 user32 shell32 comdlg32 ole32)

# UNICODE : l API Win32 est appelee dans sa variante W partout.
target_compile_definitions(gastinel_write PRIVATE UNICODE _UNICODE NOMINMAX)

if(MSVC)
    # /utf-8 : les sources contiennent des litteraux accentues, MSVC les
    # lirait sinon dans la page de codes du systeme.
    target_compile_options(gastinel_write PRIVATE /W4 /permissive- /utf-8)
    # wWinMain comme point d entree du sous-systeme Windows.
    set_target_properties(gastinel_write PROPERTIES LINK_FLAGS "/ENTRY:wWinMainCRTStartup")
else()
    target_compile_options(gastinel_write PRIVATE -Wall -Wextra)
    # MinGW ne deduit pas wWinMain tout seul : il faut le point d entree large.
    target_link_options(gastinel_write PRIVATE -municode)
endif()

# Les assets sont copies a cote du binaire pour que l executable de build se
# comporte comme l executable installe.
add_custom_command(TARGET gastinel_write POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:gastinel_write>/assets
    COMMENT "Copie des assets (police Inter) a cote du binaire"
)

# --- Verification des E/S documents ------------------------------------------
# Cible separee, non liee au binaire applicatif : elle ne depend que de doc/,
# donc elle se compile et sexecute sans fenetre ni contexte graphique.
add_executable(gw_doc_roundtrip
    tools/doc_roundtrip.cpp
    src/doc/style.cpp
    src/doc/txt_io.cpp
    src/doc/odt_io.cpp
    src/doc/zip.cpp
    src/doc/file_bytes.cpp
    src/compress/checksum.cpp
    src/compress/inflate.cpp
    src/compress/compress_lz.cpp
)
target_include_directories(gw_doc_roundtrip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(gw_doc_roundtrip PRIVATE NOMINMAX)
if(MSVC)
    target_compile_options(gw_doc_roundtrip PRIVATE /W4 /permissive-)
else()
    target_compile_options(gw_doc_roundtrip PRIVATE -Wall -Wextra)
endif()

# --- Verification du decodeur DEFLATE et du PNG maison -----------------------
# Compare notamment le decodeur PNG maison, pixel par pixel, a une reference
# produite hors du projet : c est la preuve qui a permis de retirer GDI+.
add_executable(gw_compress_tests
    tools/compress_tests.cpp
    src/compress/checksum.cpp
    src/compress/inflate.cpp
    src/compress/compress_lz.cpp
    src/image/png.cpp
)
target_include_directories(gw_compress_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(gw_compress_tests PRIVATE NOMINMAX)
if(MSVC)
    target_compile_options(gw_compress_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(gw_compress_tests PRIVATE -Wall -Wextra)
endif()

# --- Verification du coeur du moteur d'edition -------------------------------
# doc/text_edit.cpp est un module pur : il ne depend ni de Win32, ni du moteur
# de rendu, ni d'une police. Cette cible n'a donc besoin de rien d'autre, et
# couvre toute la logique d'edition sans simuler une seule frappe.
add_executable(gw_edit_tests
    tools/edit_tests.cpp
    src/doc/text_edit.cpp
    src/doc/style.cpp
    src/doc/markdown_live.cpp
    src/compress/compress_lz.cpp
)
target_include_directories(gw_edit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(gw_edit_tests PRIVATE NOMINMAX)
if(MSVC)
    target_compile_options(gw_edit_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(gw_edit_tests PRIVATE -Wall -Wextra)
endif()

# --- Verification du modele de document enrichi ------------------------------
# doc/style.cpp est aussi pur que doc/text_edit.cpp : cette cible verifie les
# plages de style (application, chevauchement, survie aux mutations de texte) et
# leur integration dans le moteur d'edition, sans interface ni police.
add_executable(gw_style_tests
    tools/style_tests.cpp
    src/doc/style.cpp
    src/doc/text_edit.cpp
    src/doc/markdown_live.cpp
    src/compress/compress_lz.cpp
)
target_include_directories(gw_style_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(gw_style_tests PRIVATE NOMINMAX)
if(MSVC)
    target_compile_options(gw_style_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(gw_style_tests PRIVATE -Wall -Wextra)
endif()

# --- Verification des reglages et de l'import de police ----------------------
# Cible console : c'est le seul chemin ou un fichier fourni par l'utilisateur
# traverse le parser TTF, donc le seul qui merite un banc dedie. Elle ecrit dans
# le vrai fichier de reglages et le restaure a la sortie (cf. entete du .cpp).
add_executable(gw_settings_tests
    tools/settings_tests.cpp
    src/app/quotes.cpp
    src/app/settings.cpp
    src/app/theme.cpp
    src/app/i18n.cpp
    src/font/ttf.cpp
    src/doc/file_bytes.cpp
    src/platform/system.cpp
    src/platform/unicode.cpp
    src/platform/log.cpp
)
target_include_directories(gw_settings_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(gw_settings_tests PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(gw_settings_tests PRIVATE shell32 ole32)
if(MSVC)
    target_compile_options(gw_settings_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(gw_settings_tests PRIVATE -Wall -Wextra)
endif()

# --- Rendu hors fenetre des ecrans -------------------------------------------
# Compose un ecran complet dans un Framebuffer et l'ecrit en BMP, sans creer de
# fenetre : c'est possible parce que tout le rendu est CPU et que les backends
# ne font que presenter le resultat. Sert a verifier une modification
# d'interface (mode sombre, mise en page) en regardant des pixels plutot qu'en
# cliquant.
add_executable(gw_screen_render
    tools/screen_render.cpp
    src/app/chrome.cpp
    src/app/context_menu.cpp
    src/app/editor_view.cpp
    src/app/format_bar.cpp
    src/app/fonts.cpp
    src/app/home_screen.cpp
    src/app/menu_bar.cpp
    src/app/recent_files.cpp
    src/app/settings.cpp
    src/app/settings_screen.cpp
    src/app/theme.cpp
    src/app/i18n.cpp
    src/render/rasterizer.cpp
    src/render/glyph_atlas.cpp
    src/render/draw.cpp
    src/render/icons.cpp
    src/render/text_run.cpp
    src/font/ttf.cpp
    src/doc/file_bytes.cpp
    src/doc/text_edit.cpp
    src/doc/style.cpp
    src/doc/markdown_live.cpp
    src/compress/compress_lz.cpp
    src/platform/system.cpp
    src/platform/unicode.cpp
    src/platform/log.cpp
)
target_include_directories(gw_screen_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(gw_screen_render PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(gw_screen_render PRIVATE shell32 ole32)
if(MSVC)
    target_compile_options(gw_screen_render PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(gw_screen_render PRIVATE -Wall -Wextra)
endif()

# --- Verification des nouvelles fonctionnalités (i18n, curseur fluide) -------
add_executable(gw_feature_tests
    tools/feature_tests.cpp
    src/app/i18n.cpp
    src/app/settings.cpp
    src/app/editor_view.cpp
    src/app/theme.cpp
    src/app/quotes.cpp
    src/app/fonts.cpp
    src/render/rasterizer.cpp
    src/render/glyph_atlas.cpp
    src/render/draw.cpp
    src/render/icons.cpp
    src/render/text_run.cpp
    src/font/ttf.cpp
    src/doc/file_bytes.cpp
    src/doc/text_edit.cpp
    src/doc/style.cpp
    src/doc/markdown_live.cpp
    src/compress/compress_lz.cpp
    src/platform/system.cpp
    src/platform/unicode.cpp
    src/platform/log.cpp
)
target_include_directories(gw_feature_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(gw_feature_tests PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(gw_feature_tests PRIVATE shell32 ole32)
if(MSVC)
    target_compile_options(gw_feature_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(gw_feature_tests PRIVATE -Wall -Wextra)
endif()
