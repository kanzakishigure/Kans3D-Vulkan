# copy_assets.cmake
#Input：SRC_DIR, DST_DIR, DIR_NAME


# Editor icons and other bundled resources must also reach existing builds.
if(EXISTS "${DST_DIR}" AND NOT DIR_NAME STREQUAL "Resources")
    message(STATUS "[Asset Sync] Skipped '${DIR_NAME}' (already exists)")
    return()
endif()

file(COPY "${SRC_DIR}/" DESTINATION "${DST_DIR}")
message(STATUS "[Asset Sync] Copied  '${DIR_NAME}' -> ${DST_DIR}")
