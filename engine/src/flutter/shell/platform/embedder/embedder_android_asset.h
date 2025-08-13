// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_ANDROID_ASSET_H_
#define FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_ANDROID_ASSET_H_

#include <memory>

#include "flutter/fml/build_config.h"
#include "flutter/fml/mapping.h"

namespace flutter {

#if FML_OS_ANDROID

//------------------------------------------------------------------------------
/// @brief      A utility method to create one off Android asset mappings. This
///             method is only used by the embedder on Android prior to the
///             asset manager setup.
///
/// @param[in]  asset_path  The asset path.
///
/// @return     The asset mapping on Android if one can be setup.
///
std::unique_ptr<fml::Mapping> CreateAndroidAssetMapping(const char* asset_path);

#endif  // FML_OS_ANDROID

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_ANDROID_ASSET_H_
