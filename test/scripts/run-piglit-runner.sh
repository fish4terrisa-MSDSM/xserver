#!/bin/sh

# Wrapper script for invoking piglit-runner
# (https://crates.io/crates/deqp-runner) with the PIGLIT_DIR from the
# environment, or emitting a skip exit code if not set.

set -e

if test "x$PIGLIT_DIR" = "x"; then
    echo "PIGLIT_DIR must be set to the directory of the piglit repository."
    # Exit as a "skip" so make check works even without piglit.
    exit 77
fi

if test "x$XSERVER_BUILDDIR" = "x"; then
    echo "XSERVER_BUILDDIR must be set to the build directory of the xserver repository."
    # Exit as a real failure because it should always be set.
    exit 1
fi

$XSERVER_BUILDDIR/test/simple-xinit \
    piglit-runner run \
    --piglit-folder $PIGLIT_DIR \
    -j${FDO_CI_CONCURRENT:-4} \
    "$@"
