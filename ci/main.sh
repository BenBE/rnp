#!/bin/bash
set -eux

[ "$BUILD_MODE" = "style-check" ] && exec ci/style-check.sh

: "${CORES:=2}"
: "${RNP_TESTS:=all}"

# check for use of uninitialized or unused vars in CMake
function cmake {
  log=$(mktemp)
  command cmake --warn-uninitialized --warn-unused "$@" 2>&1 | tee "$log"
  if grep -Fqi 'cmake warning' "$log"; then exit 1; fi
}

cmakeopts=(
  "-DCMAKE_BUILD_TYPE=Debug"
  "-DBUILD_SHARED_LIBS=yes"
  "-DCMAKE_INSTALL_PREFIX=${RNP_INSTALL}"
  "-DCMAKE_PREFIX_PATH=${BOTAN_INSTALL};${CMOCKA_INSTALL};${JSONC_INSTALL};${GPG_INSTALL}"
  "-Dcmocka_DIR=${CMOCKA_INSTALL}/cmocka"
)
[ "$BUILD_MODE" = "coverage" ] && cmakeopts+=("-DENABLE_COVERAGE=yes")
[ "$BUILD_MODE" = "sanitize" ] && cmakeopts+=("-DENABLE_SANITIZERS=yes")

mkdir build
pushd build
export LD_LIBRARY_PATH="${GPG_INSTALL}/lib"

cmake "${cmakeopts[@]}" ..
make -j${CORES} VERBOSE=1 install

: "${COVERITY_SCAN_BRANCH:=0}"
[[ ${COVERITY_SCAN_BRANCH} = 1 ]] && exit 0

CTEST="ctest"
[ "$BUILD_MODE" = "valgrind" ] && CTEST+=" -T memcheck"
${CTEST} -V -R rnp_tests
popd

exit 0

