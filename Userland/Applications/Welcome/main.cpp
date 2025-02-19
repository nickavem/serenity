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

#include "BackgroundWidget.h"
#include "TextWidget.h"
#include "UnuncheckableButton.h"
#include <AK/ByteBuffer.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/StringBuilder.h>
#include <AK/Vector.h>
#include <LibCore/File.h>
#include <LibGUI/Application.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Button.h>
#include <LibGUI/ImageWidget.h>
#include <LibGUI/Label.h>
#include <LibGUI/MessageBox.h>
#include <LibGUI/StackWidget.h>
#include <LibGUI/Window.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/Font.h>
#include <LibGfx/FontDatabase.h>
#include <stdio.h>
#include <unistd.h>

struct ContentPage {
    String menu_name;
    String title;
    String icon = String::empty();
    Vector<String> content;
};

static Optional<Vector<ContentPage>> parse_welcome_file(const String& path)
{
    auto file = Core::File::construct(path);
    if (!file->open(Core::IODevice::ReadOnly))
        return {};

    Vector<ContentPage> pages;
    StringBuilder current_output_line;
    bool started = false;
    ContentPage current;
    while (file->can_read_line()) {
        auto line = file->read_line();
        if (line.is_empty()) {
            if (!current_output_line.to_string().is_empty())
                current.content.append(current_output_line.to_string());
            current_output_line.clear();
            continue;
        }
        switch (line[0]) {
        case '*':
            dbgln("menu_item line:\t{}", line);
            if (started)
                pages.append(current);
            else
                started = true;

            current = {};
            current.menu_name = line.substring(2, line.length() - 2);
            break;
        case '$':
            dbgln("icon line: \t{}", line);
            current.icon = line.substring(2, line.length() - 2);
            break;
        case '>':
            dbgln("title line:\t{}", line);
            current.title = line.substring(2, line.length() - 2);
            break;
        case '#':
            dbgln("comment line:\t{}", line);
            break;
        default:
            dbgln("content line:\t", line);
            if (current_output_line.length() != 0)
                current_output_line.append(' ');
            current_output_line.append(line);
            break;
        }
    }

    if (started) {
        current.content.append(current_output_line.to_string());
        pages.append(current);
    }

    return pages;
}

int main(int argc, char** argv)
{
    if (pledge("stdio sendfd shared_buffer rpath unix cpath fattr", nullptr) < 0) {
        perror("pledge");
        return 1;
    }

    auto app = GUI::Application::construct(argc, argv);

    if (pledge("stdio sendfd shared_buffer rpath", nullptr) < 0) {
        perror("pledge");
        return 1;
    }

    if (unveil("/res", "r") < 0) {
        perror("unveil");
        return 1;
    }

    unveil(nullptr, nullptr);

    Optional<Vector<ContentPage>> _pages = parse_welcome_file("/res/welcome.txt");
    if (!_pages.has_value()) {
        GUI::MessageBox::show(nullptr, "Could not open Welcome file.", "Welcome", GUI::MessageBox::Type::Error);
        return 1;
    }
    auto pages = _pages.value();

    auto window = GUI::Window::construct();
    window->set_title("Welcome");
    window->resize(640, 360);
    window->center_on_screen();

    auto& background = window->set_main_widget<BackgroundWidget>();
    background.set_fill_with_background_color(false);
    background.set_layout<GUI::VerticalBoxLayout>();
    background.layout()->set_margins({ 16, 8, 16, 8 });
    background.layout()->set_spacing(8);

    //
    // header
    //

    auto& header = background.add<GUI::Label>();
    header.set_font(Gfx::Font::load_from_file("/res/fonts/PebbletonBold14.font"));
    header.set_text("Welcome to SerenityOS!");
    header.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    header.set_fixed_height(30);

    //
    // main section
    //

    auto& main_section = background.add<GUI::Widget>();
    main_section.set_layout<GUI::HorizontalBoxLayout>();
    main_section.layout()->set_margins({ 0, 0, 0, 0 });
    main_section.layout()->set_spacing(8);

    auto& menu = main_section.add<GUI::Widget>();
    menu.set_layout<GUI::VerticalBoxLayout>();
    menu.layout()->set_margins({ 0, 0, 0, 0 });
    menu.layout()->set_spacing(4);
    menu.set_fixed_width(100);

    auto& stack = main_section.add<GUI::StackWidget>();

    bool first = true;
    for (auto& page : pages) {
        auto& content = stack.add<GUI::Widget>();
        content.set_layout<GUI::VerticalBoxLayout>();
        content.layout()->set_margins({ 0, 0, 0, 0 });
        content.layout()->set_spacing(8);

        auto& title_box = content.add<GUI::Widget>();
        title_box.set_layout<GUI::HorizontalBoxLayout>();
        title_box.layout()->set_spacing(4);
        title_box.set_fixed_height(16);

        if (!page.icon.is_empty()) {
            auto& icon = title_box.add<GUI::ImageWidget>();
            icon.set_fixed_size(16, 16);
            icon.load_from_file(page.icon);
        }

        auto& content_title = title_box.add<GUI::Label>();
        content_title.set_font(Gfx::FontDatabase::default_bold_font());
        content_title.set_text(page.title);
        content_title.set_text_alignment(Gfx::TextAlignment::CenterLeft);
        content_title.set_fixed_height(10);

        for (auto& paragraph : page.content) {
            auto& content_text = content.add<TextWidget>();
            content_text.set_font(Gfx::FontDatabase::default_font());
            content_text.set_text(paragraph);
            content_text.set_text_alignment(Gfx::TextAlignment::TopLeft);
            content_text.set_line_height(12);
            content_text.wrap_and_set_height();
        }

        auto& menu_option = menu.add<UnuncheckableButton>();
        menu_option.set_font(Gfx::FontDatabase::default_font());
        menu_option.set_text(page.menu_name);
        menu_option.set_text_alignment(Gfx::TextAlignment::CenterLeft);
        menu_option.set_fixed_height(20);
        menu_option.set_checkable(true);
        menu_option.set_exclusive(true);

        if (first)
            menu_option.set_checked(true);

        menu_option.on_click = [content = &content, &stack](auto) {
            stack.set_active_widget(content);
            content->invalidate_layout();
        };

        first = false;
    }

    window->show();
    return app->exec();
}
