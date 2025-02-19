/*
 * Copyright (c) 2020, Peter Elliott <pelliott@ualberta.ca>
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

#include <AK/Types.h>
#include <LibCore/Account.h>
#include <LibCore/ArgsParser.h>
#include <LibCore/File.h>
#include <LibCore/GetPassword.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    if (geteuid() != 0) {
        warnln("Not running as root :^(");
        return 1;
    }

    if (pledge("stdio wpath rpath cpath tty id", nullptr) < 0) {
        perror("pledge");
        return 1;
    }

    if (unveil("/etc/passwd", "rwc") < 0) {
        perror("unveil");
        return 1;
    }

    if (unveil("/etc/group", "rwc") < 0) {
        perror("unveil");
        return 1;
    }

    if (unveil("/etc/shadow", "rwc") < 0) {
        perror("unveil");
        return 1;
    }

    unveil(nullptr, nullptr);

    bool del = false;
    bool lock = false;
    bool unlock = false;
    const char* username = nullptr;

    auto args_parser = Core::ArgsParser();
    args_parser.set_general_help("Modify an account password.");
    args_parser.add_option(del, "Delete password", "delete", 'd');
    args_parser.add_option(lock, "Lock password", "lock", 'l');
    args_parser.add_option(unlock, "Unlock password", "unlock", 'u');
    args_parser.add_positional_argument(username, "Username", "username", Core::ArgsParser::Required::No);

    args_parser.parse(argc, argv);

    uid_t current_uid = getuid();

    auto account_or_error = (username)
        ? Core::Account::from_name(username, Core::Account::OpenPasswdFile::ReadWrite, Core::Account::OpenShadowFile::ReadWrite)
        : Core::Account::from_uid(current_uid, Core::Account::OpenPasswdFile::ReadWrite, Core::Account::OpenShadowFile::ReadWrite);

    if (account_or_error.is_error()) {
        warnln("Core::Account::{}: {}", (username) ? "from_name" : "from_uid", account_or_error.error());
        return 1;
    }

    // Drop privileges after opening all the files through the Core::Account object.
    auto gid = getgid();
    if (setresgid(gid, gid, gid) < 0) {
        perror("setresgid");
        return 1;
    }

    auto uid = getuid();
    if (setresuid(uid, uid, uid) < 0) {
        perror("setresuid");
        return 1;
    }

    // Make sure /etc/passwd is open and ready for reading, then we can drop a bunch of pledge promises.
    setpwent();

    if (pledge("stdio tty", nullptr) < 0) {
        perror("pledge");
        return 1;
    }

    // target_account is the account we are changing the password of.
    auto& target_account = account_or_error.value();

    if (current_uid != 0 && current_uid != target_account.uid()) {
        warnln("You can't modify passwd for {}", username);
        return 1;
    }

    if (del) {
        target_account.delete_password();
    } else if (lock) {
        target_account.set_password_enabled(false);
    } else if (unlock) {
        target_account.set_password_enabled(true);
    } else {
        auto new_password = Core::get_password("New password: ");
        if (new_password.is_error()) {
            warnln("{}", new_password.error());
            return 1;
        }

        target_account.set_password(new_password.value().characters());
    }

    if (pledge("stdio", nullptr) < 0) {
        perror("pledge");
        return 1;
    }

    if (!target_account.sync()) {
        perror("Core::Account::Sync");
    }

    return 0;
}
