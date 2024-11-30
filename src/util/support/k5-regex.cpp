/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/* vi: set softtabstop=4 shiftwidth=4 tabstop=4 expandtab ai: */
/* k5-regex.cpp - Glue routines to std::regex functions */

/*
 * These functions provide a mostly-complete POSIX regex(3)
 * implementation that uses the C++ std::regex classes.  Deficiencies
 * are noted below.
 */

#include <k5-platform.h>
#include "k5-regex.h"

#include <regex>

/*
 * Our implementation of regcomp() which calls into std::regex.  We
 * implement the standard flags but none of the non-portable extensions
 * on some platforms
 */

extern "C" int
k5_regcomp(regex_t *preg, const char *pattern, int cflags)
{
    std::regex *r;
    std::regex_constants::syntax_option_type flags;
    int retcode = 0;

    if (cflags & REG_EXTENDED)
        flags = std::regex::extended;
    else
        flags = std::regex::basic;

    if (cflags & REG_ICASE)
        flags |= std::regex::icase;

    /*
     * If std::regex::nosubs is set then we won't get any submatches,
     * but we don't need to do anything here, regexec() will handle it
     * just fine.
     */

    if (cflags & REG_NOSUB)
        flags |= std::regex::nosubs;

    /*
     * If regex_t is allocated on the stack we can't guarantee that
     * the regex pointer is initialized to NULL so calling regfree()
     * on that might fail.  Set the regex pointer to NULL in case
     * the regular expression compilation fails.
     */

    preg->regex = NULL;
    preg->re_nsub = 0;

    try {
        r = new std::regex(pattern, flags);
        preg->regex = r;
        preg->re_nsub = r->mark_count();
    } catch (std::regex_error& e) {
        /*
         * A note here: in POSIX regex.h REG_NOMATCH seems to be defined
         * as "1" and all other error codes are beyond that.  But there's
         * no guarantee that any of the exception error codes won't
         * overlap with our definition of REG_NOMATCH (also 1), so simply
         * add 2 to whatever code we get and subtract 2 inside of
         * k5_regerror().
         */
        retcode = e.code() + 2;
    }

    return retcode;
}

extern "C" int
k5_regexec(const regex_t *preg, const char *string, size_t nmatch,
           regmatch_t pmatch[], int eflags)
{
    int retcode = 0;
    unsigned int i;
    std::cmatch cm;
    std::regex_constants::match_flag_type flags =
                                        std::regex_constants::match_default;
    std::regex *r = static_cast<std::regex *>(preg->regex);

    if (eflags & REG_NOTBOL)
        flags |= std::regex_constants::match_not_bol;

    if (eflags & REG_NOTEOL)
        flags |= std::regex_constants::match_not_eol;

    try {
        if (! std::regex_search(string, cm, *r, flags))
            return REG_NOMATCH;

        /*
         * If given, fill in pmatch with the full match string and any
         * sub-matches.  If we set nosub previously we shouldn't have
         * any submatches (but should still have the first element
         * which refers to the whole match string).
         */

        for (i = 0; i < nmatch; i++) {
            /*
             * If we're past the end of the match list (cm.size()) or
             * this sub-match didn't match (!cm[i].matched()) then
             * return -1 for those array members.
             */
            if (i >= cm.size() || !cm[i].matched) {
                pmatch[i].rm_so = pmatch[i].rm_eo = -1;
            } else {
                pmatch[i].rm_so = cm.position(i);
                pmatch[i].rm_eo = cm.position(i) + cm.length(i);
            }
        }
    } catch (std::regex_error& e) {
        /* See above */
        retcode = e.code() + 2;
    }

    return retcode;
}

/*
 * Report back an error string.  I wish we could have figured out a way
 * to save the error string from std::regex_error, but the only place to
 * stash it was in regex_t and there is no requirement to call regfree()
 * if regcomp() fails.  I could have had a fixed array inside of regexec_t
 * but that seemed sub-optimal as well.  Maybe think about revisiting that.
 */

extern "C" size_t
k5_regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size)
{
    static const char *err;

    /* See above */
    switch (errcode - 2) {
    case std::regex_constants::error_collate:
        err = "Invalid collating element in regular expression";
        break;
    case std::regex_constants::error_ctype:
        err = "Invalid character class name in regular expression";
        break;
    case std::regex_constants::error_escape:
        err = "Invalid escaped character or trailing escape in "
              "regular expression";
        break;
    case std::regex_constants::error_backref:
        err = "Invalid back reference in regular expression";
        break;
    case std::regex_constants::error_brack:
        err = "Mismatched '[' and ']' in regular expression";
        break;
    case std::regex_constants::error_paren:
        err = "Mismatched '(' and ')' in regular expression";
        break;
    case std::regex_constants::error_brace:
        err = "Mismatched '{' and '}' in regular expression";
        break;
    case std::regex_constants::error_badbrace:
        err = "Invalid range in '{}' expression in regular expression";
        break;
    case std::regex_constants::error_range:
        err = "Invalid character range in regular expression";
        break;
    case std::regex_constants::error_space:
        err = "Not enough memory to convert expression into "
              "finite state machine";
        break;
    case std::regex_constants::error_badrepeat:
        err = "'*', '?', '+', or '{' was not preceded by a valid "
              "regular expression";
        break;
    case std::regex_constants::error_complexity:
        err = "Complexity of attempted match exceeded a predefined level";
        break;
    case std::regex_constants::error_stack:
        err = "Not enough memory to perform a match";
        break;
    default:
        err = "Unknown regular expression error";
    }

    if (errbuf && errbuf_size > 0) {
        strncpy(errbuf, err, errbuf_size);
        errbuf[errbuf_size - 1] = '\0';
    }

    return strlen(err);
}

extern "C" void
k5_regfree(regex_t *preg)
{
    if (preg->regex) {
        delete static_cast<std::regex *>(preg->regex);
            preg->regex = NULL;
    }
}
