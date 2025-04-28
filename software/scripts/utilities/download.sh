#!/bin/bash

if [ $# -ne 1 ]; then
  echo "usage: download <version>"
  echo "where <version> is of the form v#.#.#, e.g. v1.3.4"
  exit 1
fi

curl --header "PRIVATE-TOKEN: glpat-SuLs9zsz2yNGnkwvvsKy" https://gitlab.leaflabs.com/api/v4/projects/519/packages/generic/module_firmware/$1/pellet_module_fw_$1.bin --output pellet_module_$1

FILE_SIZE=$(stat -c%s "pellet_module_$1")
MIN_SIZE=102400

echo $FILE_SIZE
if [ $FILE_SIZE -lt $MIN_SIZE ]; then
  echo "Version $1 not supported. Choose another version"
  rm pellet_module_$1
  exit 1
fi

curl --header "PRIVATE-TOKEN: glpat-SuLs9zsz2yNGnkwvvsKy" https://gitlab.leaflabs.com/api/v4/projects/519/packages/generic/module_firmware/$1/magnet_module_fw_$1.bin --output magnet_module_$1

curl --header "PRIVATE-TOKEN: glpat-SuLs9zsz2yNGnkwvvsKy" https://gitlab.leaflabs.com/api/v4/projects/519/packages/generic/host_tools/$1/jerrycan_updater_$1_arm64 --output jerrycan_updater

pip install --force-reinstall pyjerrycan==$1

