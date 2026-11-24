if(TARGET hpcc)
  build_exec(
    EXECNAME third
    EXECNAME_PREFIX scratch_hpcc_
    SOURCE_FILES third.cc
    LIBRARIES_TO_LINK ${libhpcc}
    EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/hpcc/
  )
endif()
