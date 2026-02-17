# Utility function to compile GLSL -> SPIR-V and expose the generated .spv files
# Usage: compile_shaders(<out-dir> <src-dir> <shader1> <shader2> ...)
function(compile_shaders OUT_DIR SRC_DIR)
  find_program(GLSLANG_VALIDATOR glslangValidator glslc)
  if (NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator or glslc is required to compile shaders")
  endif()

  file(MAKE_DIRECTORY ${OUT_DIR})
  set(spv_files "")
  foreach(shader IN LISTS ARGN)
    set(in ${SRC_DIR}/${shader})
    # keep original shader filename (triangle.vert -> triangle.vert.spv)
    set(out ${OUT_DIR}/${shader}.spv)
    
    # Determine if this is an RTX shader and add appropriate flags
    set(shader_flags "-V")
    if(shader MATCHES "\\.(rgen|rchit|rmiss|rint|rahit)$")
      set(shader_flags "-V" "--target-env" "vulkan1.2")
    endif()
    
    add_custom_command(
      OUTPUT ${out}
      COMMAND ${GLSLANG_VALIDATOR} ${shader_flags} ${in} -o ${out}
      DEPENDS ${in}
      COMMENT "Compiling ${shader} to SPIR-V"
    )
    list(APPEND spv_files ${out})
  endforeach()

  add_custom_target(Shaders ALL DEPENDS ${spv_files})
  set(COMPILED_SHADER_SPVS ${spv_files} PARENT_SCOPE)
endfunction()