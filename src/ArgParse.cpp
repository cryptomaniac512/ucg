/*
 * Copyright 2015 Gary R. Van Sickle (grvs@users.sourceforge.net).
 *
 * This file is part of UniversalCodeGrep.
 *
 * UniversalCodeGrep is free software: you can redistribute it and/or modify it under the
 * terms of version 3 of the GNU General Public License as published by the Free
 * Software Foundation.
 *
 * UniversalCodeGrep is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * UniversalCodeGrep.  If not, see <http://www.gnu.org/licenses/>.
 */

/** @file */

#include "ArgParse.h"

#include <vector>
#include <string>
#include <thread>
#include <argp.h>
#include <cstdlib>

#include "config.h"

static const char *argp_program_version = PACKAGE_STRING "\n"
	"Copyright (C) 2015 Gary R. Van Sickle.\n"
	"\n"
	"This program is free software; you can redistribute it and/or modify\n"
	"it under the terms of version 3 of the GNU General Public License as\n"
	"published by the Free Software Foundation.\n"
	"\n"
	"This program is distributed in the hope that it will be useful,\n"
	"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	"GNU General Public License for more details.\n"
	"\n"
	"You should have received a copy of the GNU General Public License\n"
	"along with this program. If not, see http://www.gnu.org/licenses/."
	;
static const char *argp_program_bug_address = PACKAGE_BUGREPORT;

static char doc[] = "ucg: the Universal Code Grep tool.";

static char args_doc[] = "PATTERN [FILES OR DIRECTORIES]";

/// @name Keys for options without short-options.
///@{
#define OPT_COLOR          1
#define OPT_NOCOLOR        2
#define OPT_IGNORE_DIR     3
#define OPT_NOIGNORE_DIR     4
///@}

static struct argp_option options[] = {
		{0,0,0,0, "Searching:" },
		{"ignore-case", 'i', 0,	0,	"Ignore case distinctions in PATTERN"},
		{0,0,0,0, "File presentation:" },
		{"color", OPT_COLOR, 0, 0, "Render the output with ANSI color codes."},
		{"colour", OPT_COLOR, 0, OPTION_ALIAS },
		{"nocolor", OPT_NOCOLOR, 0, 0, "Render the output without ANSI color codes."},
		{"nocolour", OPT_NOCOLOR, 0, OPTION_ALIAS },
		{0,0,0,0, "File inclusion/exclusion:"},
		{"ignore-dir",  OPT_IGNORE_DIR, "REGEX", 0,  "Exclude directories matching this regex pattern."},
		{"noignore-dir",  OPT_NOIGNORE_DIR, "REGEX", 0,  "Do not exclude directories matching this regex pattern."},
		{0,0,0,0, "Miscellaneous:" },
		{"jobs",  'j', "NUM_JOBS",      0,  "Number of scanner jobs (std::thread<>s) to use" },
		{ 0 }
	};

error_t parse_opt (int key, char *arg, struct argp_state *state)
{
	class ArgParse *arguments = state->input;

	switch (key)
	{
	case 'i':
		arguments->m_ignore_case = true;
		break;
	case OPT_IGNORE_DIR:
		arguments->m_excludes.push_back(arg);
		break;
	case OPT_NOIGNORE_DIR:
		/// @todo Implement.
		///arguments->m_excludes.push_back(arg);
		break;
	case 'j':
		if(atoi(arg) < 1)
		{
			// Specified 0 or negative jobs.
			argp_failure(state, 1, 0, "jobs must be >= 1");
		}
		else
		{
			arguments->m_jobs = atoi(arg);
		}
		break;
	case OPT_COLOR:
		arguments->m_color = true;
		break;
	case OPT_NOCOLOR:
		arguments->m_color = false;
		break;
	case ARGP_KEY_ARG:
		if(state->arg_num == 0)
		{
			// First arg is the pattern.
			arguments->m_pattern = arg;
		}
		else
		{
			// Remainder are optional file paths.
			arguments->m_paths.push_back(arg);
		}
		break;
	case ARGP_KEY_END:
		if(state->arg_num < 1)
		{
			// Not enough args.
			argp_usage(state);
		}
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc };


ArgParse::ArgParse()
	: m_ignore_case(false),
	  m_jobs(0),
	  m_color(true)
{

}

ArgParse::~ArgParse()
{
	// TODO Auto-generated destructor stub
}

void ArgParse::Parse(int argc, char **argv)
{
	argp_parse(&argp, argc, argv, 0, 0, this);

	//// Now set up defaults.

	// Number of jobs.
	if(m_jobs == 0)
	{
		// Number of jobs wasn't specified on command line.  Default to the number of cores the std lib says we have.
		m_jobs = std::thread::hardware_concurrency();

		if(m_jobs == 0)
		{
			// std::thread::hardware_concurrency() is broken.  Default to one thread.
			m_jobs = 1;
		}
	}

	// Search files/directories.
	if(m_paths.empty())
	{
		// Default to current directory.
		m_paths.push_back(".");
	}

}
