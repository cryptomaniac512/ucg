/*
 * Copyright 2015-2016 Gary R. Van Sickle (grvs@users.sourceforge.net).
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

#include "config.h"

#include "Logger.h"
#include "FileScanner.h"
#include "FileScannerCpp11.h"
#include "FileScannerPCRE.h"
#include "FileScannerPCRE2.h"
#include "File.h"
#include "Match.h"
#include "MatchList.h"

#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <mutex>
#ifndef HAVE_SCHED_SETAFFINITY
#else
	#include <sched.h>
#endif

#include "ResizableArray.h"

static std::mutex f_assign_affinity_mutex;


std::unique_ptr<FileScanner> FileScanner::Create(sync_queue<std::string> &in_queue,
			sync_queue<MatchList> &output_queue,
			std::string regex,
			bool ignore_case,
			bool word_regexp,
			bool pattern_is_literal,
			RegexEngine engine)
{
	std::unique_ptr<FileScanner> retval;

	switch(engine)
	{
	case RegexEngine::CXX11:
		retval.reset(new FileScannerCpp11(in_queue, output_queue, regex, ignore_case, word_regexp, pattern_is_literal));
		break;
	case RegexEngine::PCRE:
		retval.reset(new FileScannerPCRE(in_queue, output_queue, regex, ignore_case, word_regexp, pattern_is_literal));
		break;
	case RegexEngine::PCRE2:
		retval.reset(new FileScannerPCRE2(in_queue, output_queue, regex, ignore_case, word_regexp, pattern_is_literal));
		break;
	default:
		// Should never get here.  Throw.
		/// @todo GRVS - This should be as simple as putting a C++11 "std::to_string(error_offset)" into the string below.
		///              However, there's an issue with at least Cygwin's std lib and/or gcc itself which makes to_string() unavailable
		/// 			 (see e.g. https://gcc.gnu.org/bugzilla/show_bug.cgi?id=61580 (fixed on gcc trunk 2015-11-13),
		/// 			 https://sourceware.org/ml/cygwin/2015-01/msg00251.html).  Since I don't want to wait for the fix to trickle
		/// 			 out and I don't know how widespread the issue is, we'll do it the old-fashioned way.
		std::ostringstream ss;
		ss << static_cast<int>(engine);
		throw FileScannerException(std::string("invalid RegexEngine specified: ") + ss.str());
		break;
	}

	return retval;
}

FileScanner::FileScanner(sync_queue<std::string> &in_queue,
		sync_queue<MatchList> &output_queue,
		std::string regex,
		bool ignore_case,
		bool word_regexp,
		bool pattern_is_literal) : m_ignore_case(ignore_case), m_word_regexp(word_regexp), m_pattern_is_literal(pattern_is_literal),
				m_in_queue(in_queue), m_output_queue(output_queue), m_regex(regex),
				m_next_core(0), m_use_mmap(false), m_manually_assign_cores(false)
{
}

FileScanner::~FileScanner()
{
}

void FileScanner::Run(int thread_index)
{
	// Set the name of the thread.
	std::stringstream temp_ss;
	temp_ss << "FILESCAN_";
	temp_ss << thread_index;
	set_thread_name(temp_ss.str());

	if(m_manually_assign_cores)
	{
		// Spread the scanner threads across cores.  Linux at least doesn't seem to want to do that by default.
		AssignToNextCore();
	}

	// Create a reusable, resizable buffer for the File() reads.
	auto file_data_storage = std::make_shared<ResizableArray<char>>();

	// Pull new filenames off the input queue until it's closed.
	std::string next_string;
	while(m_in_queue.wait_pull(std::move(next_string)) != queue_op_status::closed)
	{
		MatchList ml(next_string);

		try
		{
			// Try to open and read the file.  This could throw.
			LOG(INFO) << "Attempting to scan file \'" << next_string << "\'";
			File f(next_string, file_data_storage);

			if(f.size() == 0)
			{
				LOG(INFO) << "WARNING: Filesize of \'" << next_string << "\' is 0, skipping.";
				continue;
			}

			const char *file_data = f.data();
			size_t file_size = f.size();

			// Scan the file data for occurrences of the regex, sending matches to the MatchList ml.
			ScanFile(file_data, file_size, ml);

			if(!ml.empty())
			{
				// Force move semantics here.
				m_output_queue.wait_push(std::move(ml));
			}
		}
		catch(const FileException &error)
		{
			// The File constructor threw an exception.
			ERROR() << error.what();
		}
		catch(const std::system_error& error)
		{
			// A system error.  Currently should only be errors from File.
			ERROR() << error.code() << " - " << error.code().message();
		}
		catch(...)
		{
			// Rethrow whatever it was.
			throw;
		}
	}
}

void FileScanner::AssignToNextCore()
{
#ifdef HAVE_SCHED_SETAFFINITY

	// Prevent the multiple threads from stepping on each other and screwing up m_next_core.
	std::lock_guard<std::mutex> lg {f_assign_affinity_mutex};

	cpu_set_t cpuset;

	// Clear the cpu_set_t.
	CPU_ZERO(&cpuset);

	// Set the bit of the next CPU.
	CPU_SET(m_next_core, &cpuset);

	sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

	// Increment so we use the next core the next time.
	m_next_core++;
	m_next_core %= std::thread::hardware_concurrency();
#endif
}


