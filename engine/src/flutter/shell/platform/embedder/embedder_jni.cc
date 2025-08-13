// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/fml/build_config.h"

#if FML_OS_ANDROID

#include "flutter/fml/logging.h"
#include "flutter/fml/platform/android/jni_util.h"

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
  fml::jni::InitJavaVM(vm);
  FML_LOG(ERROR) << __FUNCTION__;
  return JNI_VERSION_1_4;
}

#endif  // FML_OS_ANDROID
