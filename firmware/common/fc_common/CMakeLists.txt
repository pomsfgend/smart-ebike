# Shared by the charger firmware and (optionally) the cube patch: GB/T codec + link protocol.
idf_component_register(SRCS "src/gbt_codec.c" "src/fc_link.c" INCLUDE_DIRS "include")
