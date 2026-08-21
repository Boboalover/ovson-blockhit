if(NOT DEFINED OVSON_SOURCE_DIR)
    message(FATAL_ERROR "OVSON_SOURCE_DIR is required")
endif()

set(check_count 0)

function(require_text relative_path needle description)
    file(READ "${OVSON_SOURCE_DIR}/${relative_path}" content)
    string(FIND "${content}" "${needle}" found_at)
    if(found_at EQUAL -1)
        message(FATAL_ERROR "Bed Defense restoration check failed: ${description}")
    endif()
    math(EXPR next_count "${check_count} + 1")
    set(check_count "${next_count}" PARENT_SCOPE)
endfunction()

set(core_files
    Logic/BedDefense/BedDefenseManager.cpp
    Logic/BedDefense/BedDefenseManager.h
    Logic/BedDefense/BlockHook.cpp
    Logic/BedDefense/BlockHook.h
    Render/DefenseRenderer.cpp
    Render/DefenseRenderer.h
    Render/TextureLoader.cpp
    Render/TextureLoader.h
)
foreach(core_file IN LISTS core_files)
    if(NOT EXISTS "${OVSON_SOURCE_DIR}/${core_file}")
        message(FATAL_ERROR "Bed Defense restoration check failed: missing ${core_file}")
    endif()
endforeach()
math(EXPR check_count "${check_count} + 1")

require_text(Config/Config.cpp
             "static bool g_bedDefenseEnabled = false;"
             "Bed Defense must default to disabled")
require_text(Config/Config.cpp
             "\"bedDefenseEnabled\", g_bedDefenseEnabled"
             "bedDefenseEnabled must load and save through the original key")
require_text(ClickGUI/Tabs/Utils.cpp
             "X-Ray style outlines for bed defense blocks"
             "the tabbed GUI must contain the original Bed Defense card")
require_text(ClickGUI/Render.cpp
             "{\"Bed Defense\", &Config::isBedDefenseEnabled"
             "the legacy GUI must use the original Bed Defense config accessor")
require_text(Render/RenderHook.cpp
             "BedDefense::DefenseRenderer::getInstance()->render"
             "the original renderer call must remain wired")
require_text(Logic/StatsPoll.cpp
             "BedDefense::BedDefenseManager::getInstance()->tick();"
             "the original stats-poll manager tick must remain wired")
require_text(dllmain.cpp
             "BedDefense::TextureLoader::setModule"
             "the texture loader must receive the DLL module handle")
require_text(Chat/Commands.cpp
             "registerCommand(\"bedscan\", cmd_bedscan)"
             "the original manual bed scan command must remain registered")
require_text(Config/Config.h
             "BedDetection,"
             "the original Bed Detection diagnostic category must remain available")

file(GLOB_RECURSE implementation_sources "${OVSON_SOURCE_DIR}/*.cpp")
set(stb_owner_count 0)
foreach(source_file IN LISTS implementation_sources)
    file(READ "${source_file}" source_content)
    if(source_content MATCHES "#define[ \t]+STB_IMAGE_IMPLEMENTATION")
        math(EXPR stb_owner_count "${stb_owner_count} + 1")
        if(NOT source_file STREQUAL "${OVSON_SOURCE_DIR}/Render/TextureLoader.cpp")
            message(FATAL_ERROR
                "Bed Defense restoration check failed: unexpected STB owner ${source_file}")
        endif()
    endif()
endforeach()
if(NOT stb_owner_count EQUAL 1)
    message(FATAL_ERROR
        "Bed Defense restoration check failed: expected one STB implementation owner, found ${stb_owner_count}")
endif()
math(EXPR check_count "${check_count} + 1")

file(READ "${OVSON_SOURCE_DIR}/Render/TextureLoader.cpp" texture_loader)
file(READ "${OVSON_SOURCE_DIR}/resource.h" resource_header)
foreach(resource_id RANGE 201 264)
    string(FIND "${texture_loader}" ", ${resource_id}}" mapping_at)
    if(mapping_at EQUAL -1)
        message(FATAL_ERROR
            "Bed Defense restoration check failed: TextureLoader lacks resource ID ${resource_id}")
    endif()
    string(REGEX MATCH
           "#define[ \t]+IDR_[A-Z0-9_]+[ \t]+${resource_id}([^0-9]|$)"
           resource_match "${resource_header}")
    if(resource_match STREQUAL "")
        message(FATAL_ERROR
            "Bed Defense restoration check failed: resource.h lacks resource ID ${resource_id}")
    endif()
endforeach()
file(GLOB texture_assets "${OVSON_SOURCE_DIR}/assets/blocks/*.png")
list(LENGTH texture_assets texture_asset_count)
if(NOT texture_asset_count EQUAL 64)
    message(FATAL_ERROR
        "Bed Defense restoration check failed: expected 64 block textures, found ${texture_asset_count}")
endif()
math(EXPR check_count "${check_count} + 1")

if(EXISTS "${OVSON_SOURCE_DIR}/Logic/Bedwars/BedwarsPlacementHook.cpp" OR
   EXISTS "${OVSON_SOURCE_DIR}/Logic/Bedwars/BedwarsPlacementHook.h")
    message(FATAL_ERROR
        "Bed Defense restoration check failed: experimental BedwarsPlacementHook was restored")
endif()
file(GLOB_RECURSE bedwars_sources
     "${OVSON_SOURCE_DIR}/Logic/Bedwars/*"
     "${OVSON_SOURCE_DIR}/Render/BedwarsOverlay.*"
     "${OVSON_SOURCE_DIR}/ClickGUI/Tabs/Bedwars.cpp")
foreach(bedwars_file IN LISTS bedwars_sources)
    if(IS_DIRECTORY "${bedwars_file}")
        continue()
    endif()
    file(READ "${bedwars_file}" bedwars_content)
    if(bedwars_content MATCHES
       "Module::(AntiMisplace|BedTracker)|HudId::(BedDistance|BedStatus)")
        message(FATAL_ERROR
            "Bed Defense restoration check failed: experimental bed module found in ${bedwars_file}")
    endif()
endforeach()
math(EXPR check_count "${check_count} + 1")

if(NOT check_count EQUAL 13)
    message(FATAL_ERROR "Internal restoration test count mismatch: ${check_count}")
endif()
message(STATUS "Bed Defense restoration checks passed: ${check_count}/13")
