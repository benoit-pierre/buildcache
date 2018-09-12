//--------------------------------------------------------------------------------------------------
// Copyright (c) 2018 Marcus Geelnard
//
// This software is provided 'as-is', without any express or implied warranty. In no event will the
// authors be held liable for any damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose, including commercial
// applications, and to alter it and redistribute it freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not claim that you wrote
//     the original software. If you use this software in a product, an acknowledgment in the
//     product documentation would be appreciated but is not required.
//
//  2. Altered source versions must be plainly marked as such, and must not be misrepresented as
//     being the original software.
//
//  3. This notice may not be removed or altered from any source distribution.
//--------------------------------------------------------------------------------------------------

#include <wrappers/program_wrapper.hpp>

#include <base/debug_utils.hpp>
#include <base/hasher.hpp>
#include <config/configuration.hpp>
#include <sys/perf_utils.hpp>
#include <sys/sys_utils.hpp>

#include <iostream>
#include <map>

namespace bcache {
namespace {
/// @brief A helper class for managing wrapper capabilities.
class capabilities_t {
public:
  capabilities_t(const string_list_t& cap_strings);

  bool hard_links() const {
    return m_hard_links;
  }

private:
  bool m_hard_links = false;
};

capabilities_t::capabilities_t(const string_list_t& cap_strings) {
  for (const auto& str : cap_strings) {
    if (str == "hard_links") {
      m_hard_links = true;
    }
  }
}
}  // namespace

program_wrapper_t::program_wrapper_t(const string_list_t& args, cache_t& cache)
    : m_args(args), m_cache(cache) {
}

program_wrapper_t::~program_wrapper_t() {
}

bool program_wrapper_t::handle_command(int& return_code) {
  return_code = 1;

  try {
    // Begin by resolving any response files.
    PERF_START(RESOLVE_ARGS);
    resolve_args();
    PERF_STOP(RESOLVE_ARGS);

    // Get wrapper capabilities.
    PERF_START(GET_CAPABILITIES);
    const auto capabilites = capabilities_t(get_capabilities());
    PERF_STOP(GET_CAPABILITIES);

    // Start a hash.
    hasher_t hasher;

    // Hash the preprocessed file contents.
    PERF_START(PREPROCESS);
    hasher.update(preprocess_source());
    PERF_STOP(PREPROCESS);

    // Hash the (filtered) command line flags and environment variables.
    PERF_START(FILTER_ARGS);
    hasher.update(get_relevant_arguments().join(" ", true));
    hasher.update(get_relevant_env_vars());
    PERF_STOP(FILTER_ARGS);

    // Hash the program identification (version string or similar).
    PERF_START(GET_PRG_ID);
    hasher.update(get_program_id());
    PERF_STOP(GET_PRG_ID);

    // Finalize the hash.
    const auto hash = hasher.final();

    // Check if we can use hard links.
    auto allow_hard_links = config::hard_links() && capabilites.hard_links();

    // Look up the entry in the cache.
    PERF_START(CACHE_LOOKUP);
    const auto cached_entry = m_cache.lookup(hash);
    PERF_STOP(CACHE_LOOKUP);
    if (cached_entry) {
      // Get the list of files that are expected to be generated by the command.
      PERF_START(GET_BUILD_FILES);
      const auto target_files = get_build_files();
      PERF_STOP(GET_BUILD_FILES);

      // Copy all files from the cache to their respective target paths.
      // Note: If there is a mismatch in the expected (target) files and the actual (cached) files,
      // this will throw an exception (i.e. fall back to full program execution).
      for (const auto& file : cached_entry.files) {
        const auto& target_file = target_files.at(file.first);
        const auto& source_file = file.second;
        debug::log(debug::INFO) << "Cache hit (" << hash.as_string() << "): " << source_file
                                << " => " << target_file;
        if (allow_hard_links) {
          file::link_or_copy(source_file, target_file);
        } else {
          file::copy(source_file, target_file);
        }
      }

      // Return/print the cached program results.
      std::cout << cached_entry.std_out;
      std::cerr << cached_entry.std_err;
      return_code = cached_entry.return_code;

      return true;
    }

    // Get the list of files that are expected to be generated by the command.
    cache_t::entry_t new_entry;
    PERF_START(GET_BUILD_FILES);
    new_entry.files = get_build_files();
    PERF_STOP(GET_BUILD_FILES);

    {
      std::ostringstream ss;
      for (const auto& file : new_entry.files) {
        ss << " " << file::get_file_part(file.second);
      }
      debug::log(debug::INFO) << "Cache miss (" << hash.as_string() << ")" << ss.str();
    }

    // Run the actual program command to produce the build file.
    PERF_START(RUN_FOR_MISS);
    const auto result = sys::run_with_prefix(m_args, false);
    PERF_STOP(RUN_FOR_MISS);

    // Create a new entry in the cache.
    // Note: We do not want to create cache entries for failed program runs. We could, but that
    // would run the risk of caching intermittent faults for instance.
    if (result.return_code == 0) {
      new_entry.std_out = result.std_out;
      new_entry.std_err = result.std_err;
      new_entry.return_code = result.return_code;
      PERF_START(ADD_TO_CACHE);
      m_cache.add(hash, new_entry, allow_hard_links);
      PERF_STOP(ADD_TO_CACHE);
    }

    // Everything's ok!
    // Note: Even if the program failed, we've done the expected job (running the program again
    // would just take twice the time and give the same errors).
    return_code = result.return_code;
    return true;
  } catch (std::exception& e) {
    debug::log(debug::DEBUG) << "Exception: " << e.what();
  } catch (...) {
    // Catch-all in order to not propagate exceptions any higher up (we'll return false).
    debug::log(debug::ERROR) << "UNEXPECTED EXCEPTION";
  }

  return false;
}

//--------------------------------------------------------------------------------------------------
// Default wrapper interface implementation. Wrappers are expected to override the parts that are
// relevant.
//--------------------------------------------------------------------------------------------------

void program_wrapper_t::resolve_args() {
  // Default: Do nothing.
}

string_list_t program_wrapper_t::get_capabilities() {
  // Default: No capabilities are supported.
  string_list_t capabilites;
  return capabilites;
}

std::string program_wrapper_t::preprocess_source() {
  // Default: There is no prepocessing step.
  return std::string();
}

string_list_t program_wrapper_t::get_relevant_arguments() {
  // Default: All arguments are relevant.
  return m_args;
}

std::map<std::string, std::string> program_wrapper_t::get_relevant_env_vars() {
  // Default: There are no relevant environment variables.
  std::map<std::string, std::string> env_vars;
  return env_vars;
}

std::string program_wrapper_t::get_program_id() {
  // Default: The hash of the program binary serves as the program identification.
  const auto& program_exe = m_args[0];
  hasher_t hasher;
  hasher.update_from_file(program_exe);
  return hasher.final().as_string();
}

std::map<std::string, std::string> program_wrapper_t::get_build_files() {
  // Default: There are no build files generated by the command.
  std::map<std::string, std::string> result;
  return result;
}

}  // namespace bcache
