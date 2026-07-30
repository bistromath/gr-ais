# Copyright 2020 Free Software Foundation, Inc.
#
# This file is part of GNU Radio
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

########################################################################
# Check if there is C++ code at all
########################################################################
if(NOT ais_sources)
    message(STATUS "No C++ sources... skipping python bindings")
    return()
endif(NOT ais_sources)

########################################################################
# Check for pygccxml
########################################################################
gr_python_check_module_raw("pygccxml" "import pygccxml" PYGCCXML_FOUND)

include(GrPybind)

########################################################################
# Python Bindings
########################################################################

list(APPEND ais_python_files
    invert_python.cc
    freqest_python.cc
    pdu_to_nmea_python.cc
    viterbi_cpm_cb_python.cc
    burst_sync_cc_python.cc python_bindings.cc)

gr_pybind_make_oot(ais ../../.. gr::ais "${ais_python_files}")

# copy bindings extension for use in QA test module
add_custom_command(
    TARGET ais_python
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ais_python>
            ${PROJECT_BINARY_DIR}/test_modules/gnuradio/ais/)

install(
    TARGETS ais_python
    DESTINATION ${GR_PYTHON_DIR}/gnuradio/ais
    COMPONENT pythonapi)
