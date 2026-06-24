cmake_minimum_required(VERSION 3.14)
project(pbPFL_4BandEQ VERSION 1.0.0)

# iPlug2 checkout. Override with -DIPLUG2_DIR=... if located elsewhere.
if(NOT DEFINED IPLUG2_DIR)
  set(IPLUG2_DIR "C:/Users/b_pan/Project/iPlug2" CACHE PATH "iPlug2 root directory")
endif()

# Deploy the VST3 to the system-wide VST3 folder (scanned by all DAWs).
if(WIN32 AND (NOT DEFINED IPLUG_VST3_DEPLOY_PATH OR IPLUG_VST3_DEPLOY_PATH STREQUAL ""))
  set(IPLUG_VST3_DEPLOY_PATH "C:/Program Files/Common Files/VST3" CACHE PATH "VST3 deployment path" FORCE)
endif()

# AAX SDK location (enables the AAX target). Override with -DAAX_SDK_DIR=...
if(NOT DEFINED AAX_SDK_DIR OR AAX_SDK_DIR STREQUAL "")
  if(WIN32)
    set(AAX_SDK_DIR "C:/Users/b_pan/mytools/aax_sdk/aax-sdk-2-9-0" CACHE PATH "AAX SDK directory" FORCE)
  else()
    set(AAX_SDK_DIR "$ENV{HOME}/mytools/aax-sdk-2-9-0" CACHE PATH "AAX SDK directory" FORCE)
  endif()
endif()

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

iplug_add_plugin(${PROJECT_NAME}
  SOURCES
    PbPFL4BandEQ.cpp
    PbPFL4BandEQ.h
    resources/resource.h
  WEB_RESOURCES
    resources/web/index.html
    resources/web/style.css
    resources/web/knob-render.js
    resources/web/script.js
    resources/web/pbtechlab-logoicon.png
  UI WEBVIEW
  EXCLUDE_FORMATS WAM AU AUV3 VST2 CLAP
)

# ---------------------------------------------------------------------------
# Windows: iPlug2's WEB_RESOURCES only deploys into macOS bundles. Copy the web
# UI next to each plugin binary so the Release WebView (which loads <moduleDir>/web)
# finds it. Debug loads from the source tree directly, so this is for distribution.
# ---------------------------------------------------------------------------
set(PB_WEB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/web")

if(WIN32 AND TARGET pbPFL_4BandEQ-app)
  add_custom_command(TARGET pbPFL_4BandEQ-app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PB_WEB_DIR}" "$<TARGET_FILE_DIR:pbPFL_4BandEQ-app>/web"
    COMMENT "[pbPFL] copy WebView resources next to app binary")
endif()

if(WIN32 AND TARGET pbPFL_4BandEQ-vst3)
  add_custom_command(TARGET pbPFL_4BandEQ-vst3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PB_WEB_DIR}" "$<TARGET_FILE_DIR:pbPFL_4BandEQ-vst3>/web"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PB_WEB_DIR}" "C:/Program Files/Common Files/VST3/pbPFL_4BandEQ.vst3/Contents/x86_64-win/web"
    COMMENT "[pbPFL] copy WebView resources into vst3 bundle + deployed location")
endif()

# AAX (Windows): copy the WebView resources next to the AAX binary + into the deployed
# .aaxplugin bundle, so the editor finds <moduleDir>/web (otherwise it renders black).
if(WIN32 AND TARGET pbPFL_4BandEQ-aax)
  add_custom_command(TARGET pbPFL_4BandEQ-aax POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PB_WEB_DIR}" "$<TARGET_FILE_DIR:pbPFL_4BandEQ-aax>/web"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PB_WEB_DIR}" "C:/Program Files/Common Files/Avid/Audio/Plug-Ins/pbPFL_4BandEQ.aaxplugin/Contents/x64/web"
    COMMENT "[pbPFL] copy WebView resources into aax bundle + deployed location")
endif()
