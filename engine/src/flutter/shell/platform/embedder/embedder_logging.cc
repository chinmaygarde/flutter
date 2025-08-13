// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/embedder/embedder_logging.h"

#include <iostream>

#include "flutter/fml/build_config.h"

#if FML_OS_ANDROID
#include <android/log.h>
#endif  // FML_OS_ANDROID

namespace flutter {

//------------------------------------------------------------------------------
/// @brief      A null terminated message to log to the error log.
///
/// @param[in]  message  The message
///
static void PlatformLogError(const char* message) {
#if FML_OS_ANDROID
  __android_log_write(ANDROID_LOG_ERROR, "flutter_engine", message);
#else
  std::cerr << message << std::endl;
#endif
}

FlutterEngineResult LogEmbedderError(FlutterEngineResult code,
                                     const char* reason,
                                     const char* code_name,
                                     const char* function,
                                     const char* file,
                                     int line) {
#if FML_OS_WIN
  constexpr char kSeparator = '\\';
#else
  constexpr char kSeparator = '/';
#endif
  const auto file_base =
      (::strrchr(file, kSeparator) ? strrchr(file, kSeparator) + 1 : file);
  char error[256] = {};
  snprintf(error, (sizeof(error) / sizeof(char)),
           "%s (%d): '%s' returned '%s'. %s", file_base, line, function,
           code_name, reason);
  PlatformLogError(error);

  return code;
}

}  // namespace flutter
