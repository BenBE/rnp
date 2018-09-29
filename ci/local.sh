#!/bin/bash
set -eux

rsync -a /usr/local/rnp /tmp
sudo -iu travis bash <<EOF
cd /tmp/rnp
env ASAN_OPTIONS=fast_unwind_on_malloc=0 GPG_VERSION=$GPG_VERSION BUILD_MODE=$BUILD_MODE RNP_TESTS=all ci/run-local.sh
EOF

