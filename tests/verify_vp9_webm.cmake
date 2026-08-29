execute_process(
    COMMAND "${FFPROBE_EXECUTABLE}" -v error -select_streams v:0
            -show_entries stream=codec_name,width,height,avg_frame_rate:format=duration
            -of default=nw=1 "${INPUT_FILE}"
    RESULT_VARIABLE probe_result
    OUTPUT_VARIABLE probe_output
    ERROR_VARIABLE probe_error
)

if(NOT probe_result EQUAL 0)
    message(FATAL_ERROR "ffprobe failed: ${probe_error}")
endif()

foreach(expected
        "codec_name=vp9"
        "width=160"
        "height=128"
        "avg_frame_rate=30/1"
        "duration=1.000000")
    string(FIND "${probe_output}" "${expected}" match_position)
    if(match_position EQUAL -1)
        message(FATAL_ERROR
            "Missing '${expected}' in ffprobe output:\n${probe_output}")
    endif()
endforeach()
