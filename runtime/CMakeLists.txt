add_executable(factorio_pt_runtime
  src/main.cpp
)

target_compile_definitions(factorio_pt_runtime PRIVATE
  FACTORIO_PT_APP_NAME="Factorio-pt"
)

