# Copyright 2026 Timothy Place and the catenae contributors
#
# Interleaved against planar (bench/README.md, "The layout benchmark"; PLAN.md section 5.1):
# informational instruction counts, never a gate. Carried from DspTap's bench/layout/
# (NOTICE.md). The binaries carry their own prefix, catenae_layout_, so the ratchet's glob
# (catenae_icount_*) never sees them; bench.yml counts them with scripts/icount.py --prefix
# catenae_layout_ --report-only, which prints each count and each planar / interleaved ratio
# and fails only when a binary yields no count.
#
# Scenarios: <kind>_f32_ch<C>_<i|p|s>, kind in {gain, delay, map, strip, biquad, fir, overdrive,
# fft_fir}, C in {1, 2, 6, 12}; _s is the mono configuration in catenae::planar_section over the
# interleaved frames. At one channel the layouts are the same buffer, so only _i is built. Entries are name:id
# (layout_main.cpp's CATENAE_LAYOUT_KIND).
set(_catenae_layout_kinds gain:0 delay:1 map:2 strip:3 biquad:4 fir:5 overdrive:6 fft_fir:7)
set(_catenae_layout_channels 1 2 6 12)

foreach(_kind IN LISTS _catenae_layout_kinds)
    string(REPLACE ":" ";" _parts "${_kind}")
    list(GET _parts 0 _kname)
    list(GET _parts 1 _kid)
    foreach(_ch IN LISTS _catenae_layout_channels)
        set(_layouts i:0)
        if(_ch GREATER 1)
            list(APPEND _layouts p:1 s:2)
        endif()
        foreach(_layout IN LISTS _layouts)
            string(REPLACE ":" ";" _lparts "${_layout}")
            list(GET _lparts 0 _lname)
            list(GET _lparts 1 _planar)
            set(_target catenae_layout_${_kname}_f32_ch${_ch}_${_lname})
            add_executable(${_target} layout_main.cpp)
            target_compile_definitions(${_target} PRIVATE CATENAE_LAYOUT_KIND=${_kid} CATENAE_LAYOUT_CH=${_ch}
                                                          CATENAE_LAYOUT_PLANAR=${_planar})
            target_link_libraries(${_target} PRIVATE catenae_bench_support)
            target_include_directories(${_target} PRIVATE "${PROJECT_SOURCE_DIR}/examples")
            if(NOT _catenae_bare_metal)
                add_test(NAME bench.layout.${_kname}_f32_ch${_ch}_${_lname}
                         COMMAND ${CMAKE_COMMAND} -DBINARY=$<TARGET_FILE:${_target}>
                                 "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}" -P ${CMAKE_CURRENT_SOURCE_DIR}/../icount/smoke.cmake)
                set_tests_properties(bench.layout.${_kname}_f32_ch${_ch}_${_lname} PROPERTIES LABELS bench)
            endif()
        endforeach()
        # The two layouts of a pair print the same checksum: the same output, bit for bit.
        if(_ch GREATER 1 AND NOT _catenae_bare_metal)
            set(_pair ${_kname}_f32_ch${_ch})
            add_test(NAME bench.layout.${_pair}_layouts_agree
                     COMMAND ${CMAKE_COMMAND} -DFIRST=$<TARGET_FILE:catenae_layout_${_pair}_i>
                             -DSECOND=$<TARGET_FILE:catenae_layout_${_pair}_p> "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
                             -P ${CMAKE_CURRENT_SOURCE_DIR}/../icount/agree.cmake)
            set_tests_properties(bench.layout.${_pair}_layouts_agree PROPERTIES LABELS bench)
            add_test(NAME bench.layout.${_pair}_section_agrees
                     COMMAND ${CMAKE_COMMAND} -DFIRST=$<TARGET_FILE:catenae_layout_${_pair}_i>
                             -DSECOND=$<TARGET_FILE:catenae_layout_${_pair}_s> "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
                             -P ${CMAKE_CURRENT_SOURCE_DIR}/../icount/agree.cmake)
            set_tests_properties(bench.layout.${_pair}_section_agrees PROPERTIES LABELS bench)
        endif()
    endforeach()
endforeach()

# The split-complex measurement (PLAN.md section 5.2, D25): two coefficient loops over D25's
# frame (_i) and over split real and imaginary arrays (_p), counted beside the layout pairs and
# never gated. Scenarios: spectrum_<kind>_<bins>_ch<C>_<i|p>, kind product (a bin times a
# filter's) or gain (a bin times a real gain from its power), 257 or 1025 bins, 1 or 2 channels.
foreach(_kind IN ITEMS product:0 gain:1)
    string(REPLACE ":" ";" _parts "${_kind}")
    list(GET _parts 0 _kname)
    list(GET _parts 1 _kid)
    foreach(_bins IN ITEMS 257 1025)
        foreach(_ch IN ITEMS 1 2)
            foreach(_layout IN ITEMS i:0 p:1)
                string(REPLACE ":" ";" _lparts "${_layout}")
                list(GET _lparts 0 _lname)
                list(GET _lparts 1 _split)
                set(_scenario spectrum_${_kname}_${_bins}_ch${_ch}_${_lname})
                add_executable(catenae_layout_${_scenario} spectrum_main.cpp)
                target_compile_definitions(catenae_layout_${_scenario} PRIVATE CATENAE_SPECTRUM_KIND=${_kid}
                                           CATENAE_SPECTRUM_BINS=${_bins} CATENAE_SPECTRUM_CH=${_ch}
                                           CATENAE_SPECTRUM_SPLIT=${_split})
                target_link_libraries(catenae_layout_${_scenario} PRIVATE catenae_bench_support)
                if(NOT _catenae_bare_metal)
                    add_test(NAME bench.layout.${_scenario}
                             COMMAND ${CMAKE_COMMAND} -DBINARY=$<TARGET_FILE:catenae_layout_${_scenario}>
                                     "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
                                     -P ${CMAKE_CURRENT_SOURCE_DIR}/../icount/smoke.cmake)
                    set_tests_properties(bench.layout.${_scenario} PROPERTIES LABELS bench)
                endif()
            endforeach()
        endforeach()
    endforeach()
endforeach()
