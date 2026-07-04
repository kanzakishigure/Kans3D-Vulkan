# copy_assets.cmake
#Input：SRC_DIR, DST_DIR, DIR_NAME


if(EXISTS "${DST_DIR}")                                                                                                            
    message(STATUS "[Asset Sync] Skipped '${DIR_NAME}' (already exists)")                                                          
    return()                                                                                                                       
endif()                                                                                                                            
                                                                                                                                      
file(COPY "${SRC_DIR}/" DESTINATION "${DST_DIR}")                                                                                  
message(STATUS "[Asset Sync] Copied  '${DIR_NAME}' -> ${DST_DIR}")                      