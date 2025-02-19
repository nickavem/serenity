/*
 * Copyright (c) 2020, Emanuel Sprung <emanuel.sprung@gmail.com>
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

#include <AK/String.h>
#include <AK/StringBuilder.h>
#include <LibRegex/Regex.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifdef __serenity__
#    include <regex.h>
#else
#    include <LibC/regex.h>
#endif

struct internal_regex_t {
    u8 cflags;
    u8 eflags;
    OwnPtr<Regex<PosixExtended>> re;
    size_t re_pat_errpos;
    ReError re_pat_err;
    String re_pat;
    size_t re_nsub;
};

static internal_regex_t* impl_from(regex_t* re)
{
    if (!re)
        return nullptr;

    return reinterpret_cast<internal_regex_t*>(re->__data);
}

static const internal_regex_t* impl_from(const regex_t* re)
{
    return impl_from(const_cast<regex_t*>(re));
}

extern "C" {

int regcomp(regex_t* reg, const char* pattern, int cflags)
{
    if (!reg)
        return REG_ESPACE;

    // Note that subsequent uses of regcomp() without regfree() _will_ leak memory
    // This could've been prevented if libc provided a reginit() or similar, but it does not.
    reg->__data = new internal_regex_t { 0, 0, {}, 0, ReError::REG_NOERR, {}, 0 };

    auto preg = impl_from(reg);

    if (!(cflags & REG_EXTENDED))
        return REG_ENOSYS;

    preg->cflags = cflags;

    String pattern_str(pattern);
    preg->re = make<Regex<PosixExtended>>(pattern_str, PosixOptions {} | (PosixFlags)cflags | PosixFlags::SkipTrimEmptyMatches);

    auto parser_result = preg->re->parser_result;
    if (parser_result.error != regex::Error::NoError) {
        preg->re_pat_errpos = parser_result.error_token.position();
        preg->re_pat_err = (ReError)parser_result.error;
        preg->re_pat = pattern;

        dbg() << "Have Error: " << (ReError)parser_result.error;

        return (ReError)parser_result.error;
    }

    preg->re_nsub = parser_result.capture_groups_count;

    return REG_NOERR;
}

int regexec(const regex_t* reg, const char* string, size_t nmatch, regmatch_t pmatch[], int eflags)
{
    auto preg = impl_from(reg);

    if (!preg->re || preg->re_pat_err) {
        if (preg->re_pat_err)
            return preg->re_pat_err;
        return REG_BADPAT;
    }

    RegexResult result;
    if (eflags & REG_SEARCH)
        result = preg->re->search(string, PosixOptions {} | (PosixFlags)eflags);
    else
        result = preg->re->match(string, PosixOptions {} | (PosixFlags)eflags);

    if (result.success) {
        auto size = result.matches.size();
        if (size && nmatch && pmatch) {
            pmatch[0].rm_cnt = size;

            size_t match_index { 0 };
            for (size_t i = 0; i < size; ++i) {
                pmatch[match_index].rm_so = result.matches.at(i).global_offset;
                pmatch[match_index].rm_eo = pmatch[match_index].rm_so + result.matches.at(i).view.length();
                if (match_index > 0)
                    pmatch[match_index].rm_cnt = result.capture_group_matches.size();

                ++match_index;
                if (match_index >= nmatch)
                    return REG_NOERR;

                if (i < result.capture_group_matches.size()) {
                    auto capture_groups_size = result.capture_group_matches.at(i).size();
                    for (size_t j = 0; j < preg->re->parser_result.capture_groups_count; ++j) {
                        if (j >= capture_groups_size || !result.capture_group_matches.at(i).at(j).view.length()) {
                            pmatch[match_index].rm_so = -1;
                            pmatch[match_index].rm_eo = -1;
                            pmatch[match_index].rm_cnt = 0;
                        } else {
                            pmatch[match_index].rm_so = result.capture_group_matches.at(i).at(j).global_offset;
                            pmatch[match_index].rm_eo = pmatch[match_index].rm_so + result.capture_group_matches.at(i).at(j).view.length();
                            pmatch[match_index].rm_cnt = 1;
                        }

                        ++match_index;
                        if (match_index >= nmatch)
                            return REG_NOERR;
                    }
                }
            }

            if (match_index < nmatch) {
                for (size_t i = match_index; i < nmatch; ++i) {
                    pmatch[i].rm_so = -1;
                    pmatch[i].rm_eo = -1;
                    pmatch[i].rm_cnt = 0;
                }
            }
        }
        return REG_NOERR;
    } else {
        if (nmatch && pmatch) {
            pmatch[0].rm_so = -1;
            pmatch[0].rm_eo = -1;
            pmatch[0].rm_cnt = 0;
        }
    }

    return REG_NOMATCH;
}

inline static String get_error(ReError errcode)
{
    String error;
    switch ((ReError)errcode) {
    case REG_NOERR:
        error = "No error";
        break;
    case REG_NOMATCH:
        error = "regexec() failed to match.";
        break;
    case REG_BADPAT:
        error = "Invalid regular expression.";
        break;
    case REG_ECOLLATE:
        error = "Invalid collating element referenced.";
        break;
    case REG_ECTYPE:
        error = "Invalid character class type referenced.";
        break;
    case REG_EESCAPE:
        error = "Trailing \\ in pattern.";
        break;
    case REG_ESUBREG:
        error = "Number in \\digit invalid or in error.";
        break;
    case REG_EBRACK:
        error = "[ ] imbalance.";
        break;
    case REG_EPAREN:
        error = "\\( \\) or ( ) imbalance.";
        break;
    case REG_EBRACE:
        error = "\\{ \\} imbalance.";
        break;
    case REG_BADBR:
        error = "Content of \\{ \\} invalid: not a number, number too large, more than two numbers, first larger than second.";
        break;
    case REG_ERANGE:
        error = "Invalid endpoint in range expression.";
        break;
    case REG_ESPACE:
        error = "Out of memory.";
        break;
    case REG_BADRPT:
        error = "?, * or + not preceded by valid regular expression.";
        break;
    case REG_ENOSYS:
        error = "The implementation does not support the function.";
        break;
    case REG_EMPTY_EXPR:
        error = "Empty expression provided";
        break;
    }

    return error;
}

size_t regerror(int errcode, const regex_t* reg, char* errbuf, size_t errbuf_size)
{
    String error;
    auto preg = impl_from(reg);

    if (!preg)
        error = get_error((ReError)errcode);
    else
        error = preg->re->error_string(get_error(preg->re_pat_err));

    if (!errbuf_size)
        return error.length();

    if (!error.copy_characters_to_buffer(errbuf, errbuf_size))
        return 0;

    return error.length();
}

void regfree(regex_t* reg)
{
    auto preg = impl_from(reg);
    if (preg) {
        delete preg;
        reg->__data = nullptr;
    }
}
}
