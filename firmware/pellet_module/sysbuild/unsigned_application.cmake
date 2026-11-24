# Build the pellet application unsigned; tools/firmware_signing.py signs it
# afterwards with the private key held outside every repository.
set_config_bool(${ZCMAKE_APPLICATION} CONFIG_MCUBOOT_GENERATE_UNSIGNED_IMAGE y)
set_config_string(${ZCMAKE_APPLICATION} CONFIG_MCUBOOT_SIGNATURE_KEY_FILE "")
