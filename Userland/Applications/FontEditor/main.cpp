/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "FontEditor.h"
#include <AK/URL.h>
#include <LibCore/ArgsParser.h>
#include <LibDesktop/Launcher.h>
#include <LibGUI/Action.h>
#include <LibGUI/Application.h>
#include <LibGUI/FilePicker.h>
#include <LibGUI/Icon.h>
#include <LibGUI/Menu.h>
#include <LibGUI/MenuBar.h>
#include <LibGUI/MessageBox.h>
#include <LibGUI/Window.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/BitmapFont.h>
#include <LibGfx/FontDatabase.h>
#include <LibGfx/Point.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    if (pledge("stdio sendfd shared_buffer thread rpath accept unix cpath wpath fattr unix", nullptr) < 0) {
        perror("pledge");
        return 1;
    }

    auto app = GUI::Application::construct(argc, argv);

    if (pledge("stdio sendfd shared_buffer thread rpath accept cpath wpath unix", nullptr) < 0) {
        perror("pledge");
        return 1;
    }

    if (!Desktop::Launcher::add_allowed_handler_with_only_specific_urls(
            "/bin/Help",
            { URL::create_with_file_protocol("/usr/share/man/man1/FontEditor.md") })
        || !Desktop::Launcher::seal_allowlist()) {
        warnln("Failed to set up allowed launch URLs");
        return 1;
    }

    if (pledge("stdio sendfd shared_buffer thread rpath accept cpath wpath", nullptr) < 0) {
        perror("pledge");
        return 1;
    }

    const char* path = nullptr;
    Core::ArgsParser args_parser;
    args_parser.add_positional_argument(path, "The font file for editing.", "file", Core::ArgsParser::Required::No);
    args_parser.parse(argc, argv);

    RefPtr<Gfx::BitmapFont> edited_font;
    if (path == nullptr) {
        path = "/tmp/saved.font";
        edited_font = static_ptr_cast<Gfx::BitmapFont>(Gfx::FontDatabase::default_font().clone());
    } else {
        edited_font = static_ptr_cast<Gfx::BitmapFont>(Gfx::Font::load_from_file(path)->clone());
        if (!edited_font) {
            String message = String::formatted("Couldn't load font: {}\n", path);
            GUI::MessageBox::show(nullptr, message, "Font Editor", GUI::MessageBox::Type::Error);
            return 1;
        }
    }

    auto app_icon = GUI::Icon::default_icon("app-font-editor");

    auto window = GUI::Window::construct();
    window->set_icon(app_icon.bitmap_for_size(16));

    auto set_edited_font = [&](const String& path, RefPtr<Gfx::BitmapFont>&& font, Gfx::IntPoint point) {
        // Convert 256 char font to 384 char font.
        if (font->type() == Gfx::FontTypes::Default)
            font->set_type(Gfx::FontTypes::LatinExtendedA);

        window->set_title(String::formatted("{} - Font Editor", path));
        auto& font_editor_widget = window->set_main_widget<FontEditorWidget>(path, move(font));
        window->set_rect({ point, { font_editor_widget.preferred_width(), font_editor_widget.preferred_height() } });
    };
    set_edited_font(path, move(edited_font), window->position());

    auto menubar = GUI::MenuBar::construct();

    auto& app_menu = menubar->add_menu("Font Editor");
    app_menu.add_action(GUI::CommonActions::make_open_action([&](auto&) {
        Optional<String> open_path = GUI::FilePicker::get_open_filepath(window);
        if (!open_path.has_value())
            return;

        RefPtr<Gfx::BitmapFont> new_font = static_ptr_cast<Gfx::BitmapFont>(Gfx::Font::load_from_file(open_path.value())->clone());
        if (!new_font) {
            String message = String::formatted("Couldn't load font: {}\n", open_path.value());
            GUI::MessageBox::show(window, message, "Font Editor", GUI::MessageBox::Type::Error);
            return;
        }

        set_edited_font(open_path.value(), move(new_font), window->position());
    }));
    app_menu.add_action(GUI::CommonActions::make_save_action([&](auto&) {
        FontEditorWidget* editor = static_cast<FontEditorWidget*>(window->main_widget());
        editor->save_as(editor->path());
    }));
    app_menu.add_action(GUI::CommonActions::make_save_as_action([&](auto&) {
        FontEditorWidget* editor = static_cast<FontEditorWidget*>(window->main_widget());
        LexicalPath lexical_path(editor->path());
        Optional<String> save_path = GUI::FilePicker::get_save_filepath(window, lexical_path.title(), lexical_path.extension());
        if (!save_path.has_value())
            return;

        if (editor->save_as(save_path.value()))
            window->set_title(String::formatted("{} - Font Editor", save_path.value()));
    }));
    app_menu.add_separator();
    app_menu.add_action(GUI::CommonActions::make_quit_action([&](auto&) {
        app->quit();
        return;
    }));

    auto& help_menu = menubar->add_menu("Help");
    help_menu.add_action(GUI::CommonActions::make_help_action([](auto&) {
        Desktop::Launcher::open(URL::create_with_file_protocol("/usr/share/man/man1/FontEditor.md"), "/bin/Help");
    }));

    help_menu.add_action(GUI::CommonActions::make_about_action("Font Editor", app_icon, window));

    app->set_menubar(move(menubar));

    window->show();

    return app->exec();
}
