/*
 * Copyright (c) 2015, 2022, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */
#ifndef SHARE_LOGGING_LOGCONFIGURATION_HPP
#define SHARE_LOGGING_LOGCONFIGURATION_HPP

#include "logging/logLevel.hpp"
#include "memory/allStatic.hpp"
#include "utilities/globalDefinitions.hpp"

class LogOutput;
class LogDecorators;
class LogSelectionList;
class outputStream;

// Configuration of logging. Handles parsing and configuration of the logging framework,
// and manages the list of configured log outputs. The actual tag and level configuration is
// kept implicitly in the LogTagSets and their LogOutputLists. During configuration the tagsets
// are iterated over and updated accordingly.
class LogConfiguration {
 friend class VMError;
 friend class LogTestFixture;
 public:
  LogConfiguration()
    : _outputs(nullptr),
      _n_outputs(0),
      _listener_callbacks(nullptr),
      _n_listener_callbacks(0),
      _async_mode(false) {}
  // Function for listeners
  typedef void (*UpdateListenerFunction)(void);

  // Register callback for config change.
  // The callback is always called with ConfigurationLock held,
  // hence doing log reconfiguration from the callback will deadlock.
  // The main Java thread may call this callback if there is an early registration
  // else the attach listener JavaThread, started via diagnostic command, will be executing thread.
  // The main purpose of this callback is to see if a loglevel have been changed.
  // There is no way to unregister.
  void register_update_listener(UpdateListenerFunction cb);

 private:
  LogOutput**  _outputs;
  size_t       _n_outputs;

  UpdateListenerFunction*    _listener_callbacks;
  size_t                     _n_listener_callbacks;
  bool                       _async_mode;

  // Create a new output. Returns NULL if failed.
  LogOutput* new_output(const char* name, const char* options, outputStream* errstream);

  // Add an output to the list of configured outputs. Returns the assigned index.
  size_t add_output(LogOutput* out);

  // Delete a configured output. The stderr/stdout outputs can not be removed.
  // Output should be completely disabled before it is deleted.
  void delete_output(size_t idx);

  // Disable all logging to all outputs. All outputs except stdout/stderr will be deleted.
  void disable_outputs();

  // Get output index by name. Returns SIZE_MAX if output not found.
  size_t find_output(const char* name);

  // Configure output (add or update existing configuration) to log on tag-level combination using specified decorators.
  void configure_output(size_t idx, const LogSelectionList& tag_level_expression, const LogDecorators& decorators);

  // This should be called after any configuration change while still holding ConfigurationLock
  void notify_update_listeners();

  // Respectively describe the built-in and runtime dependent portions of the configuration.
  void describe_available(outputStream* out);
  void describe_current_configuration(outputStream* out);


 public:
  // Initialization and finalization of log configuration, to be run at vm startup and shutdown respectively.
  void initialize(jlong vm_start_time);
  void finalize();

  // Perform necessary post-initialization after VM startup. Enables reconfiguration of logging.
  void post_initialize();

  // Disable all logging, equivalent to -Xlog:disable.
  void disable_logging();

  // Configures logging on stdout for the given tags and level combination.
  // Intended for mappings between -XX: flags and Unified Logging configuration.
  // If exact_match is true, only tagsets with precisely the specified tags will be configured
  // (exact_match=false is the same as "-Xlog:<tags>*=<level>", and exact_match=true is "-Xlog:<tags>=<level>").
  // Tags should be specified using the LOG_TAGS macro, e.g.
  // LogConfiguration::configure_stdout(LogLevel::<level>, <true/false>, LOG_TAGS(<tags>));
  void configure_stdout(LogLevelType level, int exact_match, ...);

  // Parse command line configuration. Parameter 'opts' is the string immediately following the -Xlog: argument ("gc" for -Xlog:gc).
  bool parse_command_line_arguments(const char* opts = "all");

  // Parse separated configuration arguments (from JCmd/MBean and command line).
  bool parse_log_arguments(const char* outputstr,
                                  const char* what,
                                  const char* decoratorstr,
                                  const char* output_options,
                                  outputStream* errstream);

  // Prints log configuration to outputStream, used by JCmd/MBean.
  void describe(outputStream* out);

  // Prints usage help for command line log configuration.
  void print_command_line_help(outputStream* out);

  // Rotates all LogOutput
  void rotate_all_outputs();

  bool is_async_mode() { return _async_mode; }
  void set_async_mode(bool value) {
    _async_mode = value;
  }
};

extern class LogConfiguration LogConfiguration;

#endif // SHARE_LOGGING_LOGCONFIGURATION_HPP
