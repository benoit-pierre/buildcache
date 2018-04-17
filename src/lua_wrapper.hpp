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

#ifndef BUILDCACHE_LUA_WRAPPER_HPP_
#define BUILDCACHE_LUA_WRAPPER_HPP_

#include "compiler_wrapper.hpp"

#include <string>

extern "C" {
typedef struct lua_State lua_State;
}

namespace bcache {
class lua_wrapper_t : public compiler_wrapper_t {
public:
  lua_wrapper_t(cache_t& cache, const std::string& lua_program_path);

  static bool can_handle_command(const std::string& compiler_exe,
                                 const std::string& lua_program_path);

private:
  // A helper class for managing the Lua state.
  class runner_t {
  public:
    runner_t(const std::string& program_path);
    ~runner_t();
    lua_State* state();
    const std::string& program_path() const;

  private:
    lua_State* m_state;
    std::string m_program_path;
  };

  std::string preprocess_source(const string_list_t& args) override;
  string_list_t filter_arguments(const string_list_t& args) override;
  std::string get_compiler_id(const string_list_t& args) override;
  std::map<std::string, std::string> get_build_files(const string_list_t& args) override;

  runner_t m_runner;
};
}  // namespace bcache
#endif  // BUILDCACHE_LUA_WRAPPER_HPP_
