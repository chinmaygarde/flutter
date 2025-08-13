// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_LOGGING_H_
#define FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_LOGGING_H_

#include "flutter/shell/platform/embedder/embedder.h"

namespace flutter {

FlutterEngineResult LogEmbedderError(FlutterEngineResult code,
                                     const char* reason,
                                     const char* code_name,
                                     const char* function,
                                     const char* file,
                                     int line);

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_LOGGING_H_
